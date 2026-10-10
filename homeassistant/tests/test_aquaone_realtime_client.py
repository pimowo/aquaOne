"""Async ordering and failure tests for the bounded Luma Realtime client."""

import asyncio
from collections import deque
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from aiohttp import WSMsgType
from aquaone_fixtures import status_payload, system_payload
from custom_components.aquaone.api.errors import (
    AquaOneDeviceRejectedError,
    AquaOneTimeoutError,
)
from custom_components.aquaone.api.models import SystemInfo
from custom_components.aquaone.api.realtime import (
    RealtimeNotification,
    RealtimeStatusMetadata,
    RealtimeStreamPosition,
    RealtimeStreamStart,
    RuntimeIdentity,
)
from custom_components.aquaone.realtime_client import (
    LumaRealtimeClient,
    ReceivedRealtimeMessage,
    RealtimeClientLimits,
    RealtimeDisconnectedError,
    RealtimeLifecycleState,
    RealtimeOverflowError,
    RealtimeProtocolError,
    RealtimeRunResult,
    RealtimeWebSocketTransport,
)
from custom_components.aquaone.products.luma import LumaSnapshot, LumaStatus

RUNTIME = "0123456789ABCDEF"
OTHER_RUNTIME = "FEDCBA9876543210"
DEVICE_KEY = "luma:246F28A1B2C3"


def position(sequence=None):
    return (
        RealtimeStreamPosition.before_first()
        if sequence is None else RealtimeStreamPosition.at(sequence)
    )


def stream_start(sequence=None):
    return ReceivedRealtimeMessage(
        RealtimeStreamStart(RuntimeIdentity(RUNTIME), position(sequence)), 90
    )


def notification(sequence, runtime=RUNTIME, size=100):
    return ReceivedRealtimeMessage(
        RealtimeNotification(
            RuntimeIdentity(runtime), sequence, "luma_status_changed"
        ),
        size,
    )


def snapshot(sequence=None, runtime=RUNTIME, minor=1, device_id="246F28A1B2C3"):
    metadata = {
        "runtime_id": runtime,
        "position": {"kind": "before_first"},
    }
    if sequence is not None:
        metadata["position"] = {"kind": "at", "sequence": str(sequence)}
    return LumaSnapshot(
        SystemInfo.from_json(system_payload(
            device_id=device_id,
            api_protocol_version={"major": 1, "minor": minor},
        )),
        LumaStatus.from_json(status_payload(realtime=metadata)),
    )


class FakeTransport:
    def __init__(self, *items, connect_failures=0):
        self.items = asyncio.Queue()
        for item in items:
            self.items.put_nowait(item)
        self.connect_failures = connect_failures
        self.connect_count = 0
        self.close_count = 0
        self.connected = False

    async def async_connect(self):
        self.connect_count += 1
        if self.connect_count <= self.connect_failures:
            raise RealtimeDisconnectedError("connect failed")
        self.connected = True

    async def async_receive(self):
        item = await self.items.get()
        if isinstance(item, BaseException):
            raise item
        return item

    async def async_close(self):
        self.close_count += 1
        if self.connected:
            self.connected = False
            self.items.put_nowait(RealtimeDisconnectedError("closed"))

    def feed(self, item):
        self.items.put_nowait(item)


class FakeAdapter:
    def __init__(self, *responses):
        self.responses = deque(responses)
        self.calls = 0
        self.active = 0
        self.max_active = 0

    async def async_get_snapshot(self, expected_device_key):
        self.calls += 1
        self.active += 1
        self.max_active = max(self.max_active, self.active)
        try:
            if expected_device_key != DEVICE_KEY:
                raise AssertionError("wrong expected identity")
            if not self.responses:
                raise AssertionError("unexpected HTTP refresh")
            value = self.responses.popleft()
            if callable(value):
                value = value()
            if hasattr(value, "__await__"):
                value = await value
            if isinstance(value, BaseException):
                raise value
            return value
        finally:
            self.active -= 1


def limits(**changes):
    values = {
        "max_frame_bytes": 512,
        "max_overlap_frames": 32,
        "max_overlap_bytes": 4096,
        "connect_timeout": 0.2,
        "receive_timeout": 0.2,
        "resync_timeout": 0.5,
        "max_resync_attempts": 1,
        "initial_backoff": 0.001,
        "max_backoff": 0.002,
    }
    values.update(changes)
    return RealtimeClientLimits(**values)


async def wait_for(predicate, timeout=0.5):
    async with asyncio.timeout(timeout):
        while not predicate():
            await asyncio.sleep(0)


class RealtimeResyncTests(unittest.IsolatedAsyncioTestCase):
    async def run_until_synced(self, transport, adapter, expected_installs=1):
        installed = []
        client = LumaRealtimeClient(
            transport, adapter, DEVICE_KEY, installed.append, limits=limits()
        )
        task = asyncio.create_task(client.async_run())
        await wait_for(lambda: len(installed) >= expected_installs)
        return client, task, installed

    async def test_stream_start_and_http_equal_position(self):
        client, task, installed = await self.run_until_synced(
            FakeTransport(stream_start(1)), FakeAdapter(snapshot(1))
        )
        self.assertEqual(client.state.installed_watermark.position.sequence, 1)
        self.assertEqual(client.state.seen_position.position.sequence, 1)
        self.assertEqual(len(installed), 1)
        await client.async_close()
        self.assertEqual(await task, RealtimeRunResult.STOPPED)

    async def test_http_position_may_be_ahead_of_stream_start(self):
        client, task, _ = await self.run_until_synced(
            FakeTransport(stream_start(1)), FakeAdapter(snapshot(3))
        )
        self.assertEqual(client.state.installed_watermark.position.sequence, 3)
        await client.async_close()
        await task

    async def test_missing_stream_start_falls_back(self):
        client = LumaRealtimeClient(
            FakeTransport(notification(1)), FakeAdapter(snapshot(1)),
            DEVICE_KEY, lambda _: None, limits=limits(),
        )
        self.assertEqual(await client.async_run(), RealtimeRunResult.FALLBACK)
        self.assertIsNone(client.state)

    async def test_connect_gate_blocks_retry_after_capability_loss(self):
        allowed = True
        lifecycle_states = []

        async def lifecycle(state):
            nonlocal allowed
            lifecycle_states.append(state)
            if state is RealtimeLifecycleState.RECONNECT:
                allowed = False

        transport = FakeTransport(connect_failures=3)
        client = LumaRealtimeClient(
            transport,
            FakeAdapter(),
            DEVICE_KEY,
            lambda _: None,
            lifecycle=lifecycle,
            can_connect=lambda: allowed,
            limits=limits(max_resync_attempts=3),
        )
        self.assertEqual(await client.async_run(), RealtimeRunResult.FALLBACK)
        self.assertEqual(transport.connect_count, 1)
        self.assertEqual(
            lifecycle_states,
            [
                RealtimeLifecycleState.CONNECTING,
                RealtimeLifecycleState.RECONNECT,
                RealtimeLifecycleState.FALLBACK,
            ],
        )

    async def test_http_older_runtime_mismatch_and_api_10_fall_back(self):
        cases = (
            (stream_start(2), snapshot(1)),
            (stream_start(1), snapshot(1, runtime=OTHER_RUNTIME)),
            (stream_start(1), snapshot(1, minor=0)),
            (stream_start(1), snapshot(1, device_id="AABBCCDDEEFF")),
        )
        for first, status in cases:
            with self.subTest(status=status):
                client = LumaRealtimeClient(
                    FakeTransport(first), FakeAdapter(status), DEVICE_KEY,
                    lambda _: None, limits=limits(),
                )
                self.assertEqual(
                    await client.async_run(), RealtimeRunResult.FALLBACK
                )
                self.assertIsNone(client.state)

    async def test_overlap_is_validated_and_requests_refresh(self):
        release = asyncio.Event()
        started = asyncio.Event()

        async def delayed_initial():
            started.set()
            await release.wait()
            return snapshot(1)

        transport = FakeTransport(stream_start(1))
        adapter = FakeAdapter(delayed_initial, snapshot(3))
        installed = []
        client = LumaRealtimeClient(
            transport, adapter, DEVICE_KEY, installed.append, limits=limits()
        )
        task = asyncio.create_task(client.async_run())
        await started.wait()
        transport.feed(notification(2))
        transport.feed(notification(3))
        release.set()
        await wait_for(
            lambda: client.state is not None
            and client.state.installed_watermark.position.sequence == 3
        )
        self.assertEqual(client.state.installed_watermark.position.sequence, 3)
        self.assertEqual(client.state.seen_position.position.sequence, 3)
        self.assertEqual(adapter.max_active, 1)
        await client.async_close()
        await task

    async def test_duplicate_overlap_is_ignored_and_gap_falls_back(self):
        async def one_case(extra, expected):
            release = asyncio.Event()

            async def delayed():
                await release.wait()
                return snapshot(1)

            transport = FakeTransport(stream_start(1), *extra)
            installed = []
            client = LumaRealtimeClient(
                transport, FakeAdapter(delayed), DEVICE_KEY,
                installed.append, limits=limits(),
            )
            task = asyncio.create_task(client.async_run())
            await asyncio.sleep(0)
            release.set()
            result = await task if not expected else None
            if expected:
                await wait_for(lambda: len(installed) == 1)
                await client.async_close()
                result = await task
            return result, installed

        result, installed = await one_case([notification(1)], True)
        self.assertEqual(result, RealtimeRunResult.STOPPED)
        self.assertEqual(len(installed), 1)
        result, installed = await one_case([notification(3)], False)
        self.assertEqual(result, RealtimeRunResult.FALLBACK)
        self.assertEqual(installed, [])

    async def test_overlap_count_and_byte_limits(self):
        for custom_limits, messages in (
            (limits(max_overlap_frames=1), [notification(1), notification(2)]),
            (
                limits(max_overlap_frames=10, max_overlap_bytes=512),
                [notification(index) for index in range(1, 7)],
            ),
        ):
            release = asyncio.Event()

            async def delayed():
                await release.wait()
                return snapshot()

            transport = FakeTransport(stream_start(), *messages)
            client = LumaRealtimeClient(
                transport, FakeAdapter(delayed), DEVICE_KEY,
                lambda _: None, limits=custom_limits,
            )
            task = asyncio.create_task(client.async_run())
            await wait_for(lambda: transport.items.empty())
            release.set()
            self.assertEqual(await task, RealtimeRunResult.FALLBACK)

    async def test_http_503_and_timeout_fall_back(self):
        for failure in (
            AquaOneDeviceRejectedError("503"),
            AquaOneTimeoutError("timeout"),
        ):
            with self.subTest(failure=failure):
                client = LumaRealtimeClient(
                    FakeTransport(stream_start()), FakeAdapter(failure),
                    DEVICE_KEY, lambda _: None, limits=limits(),
                )
                self.assertEqual(
                    await client.async_run(), RealtimeRunResult.FALLBACK
                )

    async def test_total_resync_deadline_cancels_http_work(self):
        async def blocked_http():
            await asyncio.Future()

        adapter = FakeAdapter(blocked_http)
        client = LumaRealtimeClient(
            FakeTransport(stream_start()), adapter, DEVICE_KEY,
            lambda _: None, limits=limits(resync_timeout=0.01),
        )
        self.assertEqual(await client.async_run(), RealtimeRunResult.FALLBACK)
        self.assertEqual(adapter.active, 0)


class LiveRefreshTests(unittest.IsolatedAsyncioTestCase):
    async def test_dirty_hint_runs_one_followup_without_parallel_refresh(self):
        refresh_started = asyncio.Event()
        release_refresh = asyncio.Event()

        async def first_refresh():
            refresh_started.set()
            await release_refresh.wait()
            return snapshot(1)

        transport = FakeTransport(stream_start(), notification(1))
        adapter = FakeAdapter(snapshot(), first_refresh, snapshot(2))
        installed = []
        client = LumaRealtimeClient(
            transport, adapter, DEVICE_KEY, installed.append, limits=limits()
        )
        task = asyncio.create_task(client.async_run())
        await refresh_started.wait()
        transport.feed(notification(2))
        await wait_for(lambda: client.state.seen_position.position.sequence == 2)
        release_refresh.set()
        await wait_for(
            lambda: client.state.installed_watermark.position.sequence == 2
        )
        self.assertEqual(adapter.max_active, 1)
        self.assertEqual(client.state.installed_watermark.position.sequence, 2)
        await client.async_close()
        await task

    async def test_live_duplicate_is_ignored(self):
        transport = FakeTransport(stream_start(1), notification(1))
        adapter = FakeAdapter(snapshot(1))
        client = LumaRealtimeClient(
            transport, adapter, DEVICE_KEY, lambda _: None, limits=limits()
        )
        task = asyncio.create_task(client.async_run())
        await wait_for(lambda: client.state is not None)
        await asyncio.sleep(0)
        self.assertEqual(adapter.calls, 1)
        await client.async_close()
        await task

    async def test_live_gap_runtime_change_and_stale_stream_start_fall_back(self):
        cases = (
            notification(2),
            notification(1, runtime=OTHER_RUNTIME),
            stream_start(),
        )
        for message in cases:
            with self.subTest(message=message):
                transport = FakeTransport(stream_start(), message)
                client = LumaRealtimeClient(
                    transport, FakeAdapter(snapshot()), DEVICE_KEY,
                    lambda _: None, limits=limits(),
                )
                self.assertEqual(
                    await client.async_run(), RealtimeRunResult.FALLBACK
                )

    async def test_disconnect_and_bounded_retry_signal_fallback(self):
        failures = []
        sleeps = []

        async def sleep(delay):
            sleeps.append(delay)

        client = LumaRealtimeClient(
            FakeTransport(connect_failures=3), FakeAdapter(), DEVICE_KEY,
            lambda _: None, failures.append,
            limits=limits(max_resync_attempts=3), sleep=sleep,
        )
        self.assertEqual(await client.async_run(), RealtimeRunResult.FALLBACK)
        self.assertEqual(sleeps, [0.001, 0.002])
        self.assertEqual(len(failures), 1)

    async def test_disconnect_during_live_enters_fallback(self):
        transport = FakeTransport(
            stream_start(), RealtimeDisconnectedError("peer closed")
        )
        client = LumaRealtimeClient(
            transport, FakeAdapter(snapshot()), DEVICE_KEY,
            lambda _: None, limits=limits(),
        )
        self.assertEqual(await client.async_run(), RealtimeRunResult.FALLBACK)

    async def test_http_ahead_advances_seen_without_notification_refresh(self):
        transport = FakeTransport(stream_start(), notification(1))
        adapter = FakeAdapter(snapshot(), snapshot(2))
        installed = []
        client = LumaRealtimeClient(
            transport, adapter, DEVICE_KEY, installed.append, limits=limits()
        )
        task = asyncio.create_task(client.async_run())
        await wait_for(
            lambda: client.state is not None
            and client.state.installed_watermark.position.sequence == 2
        )
        self.assertEqual(client.state.seen_position.position.sequence, 2)
        transport.feed(notification(2))
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        self.assertEqual(adapter.calls, 2)
        await client.async_close()
        await task

    async def test_external_safety_snapshot_uses_realtime_coherency_rules(self):
        transport = FakeTransport(stream_start(1))
        client, task, _ = await RealtimeResyncTests().run_until_synced(
            transport, FakeAdapter(snapshot(1))
        )
        client.accept_external_snapshot(snapshot(2))
        self.assertEqual(client.state.installed_watermark.position.sequence, 2)
        self.assertEqual(client.state.seen_position.position.sequence, 2)
        with self.assertRaises(RealtimeProtocolError):
            client.accept_external_snapshot(snapshot(2, runtime=OTHER_RUNTIME))
        await client.async_request_resync()
        await wait_for(lambda: task.done())
        self.assertEqual(await task, RealtimeRunResult.FALLBACK)

    async def test_cancellation_closes_connection_and_leaves_no_receive(self):
        transport = FakeTransport(stream_start())
        client = LumaRealtimeClient(
            transport, FakeAdapter(snapshot()), DEVICE_KEY,
            lambda _: None, limits=limits(),
        )
        task = asyncio.create_task(client.async_run())
        await wait_for(lambda: client.state is not None)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertFalse(transport.connected)
        self.assertGreaterEqual(transport.close_count, 1)


class FakeSocket:
    def __init__(self, *messages):
        self.messages = deque(messages)
        self.closed = False
        self.pongs = []

    async def receive(self, timeout):
        if not self.messages:
            await asyncio.Future()
        value = self.messages.popleft()
        if isinstance(value, BaseException):
            raise value
        return value

    async def pong(self, data):
        self.pongs.append(data)

    async def close(self):
        self.closed = True


class FakeWebSession:
    def __init__(self, socket):
        self.socket = socket
        self.calls = []
        self.closed = False

    async def ws_connect(self, url, **kwargs):
        self.calls.append((url, kwargs))
        return self.socket


class BlockingWebSession:
    async def ws_connect(self, url, **kwargs):
        await asyncio.Future()


class WebSocketTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_transport_uses_shared_session_and_handles_ping_text_close(self):
        text = json.dumps({
            "type": "stream_start", "runtime_id": RUNTIME,
            "position": {"kind": "before_first"},
        })
        socket = FakeSocket(
            SimpleNamespace(type=WSMsgType.PING, data=b"x"),
            SimpleNamespace(type=WSMsgType.TEXT, data=text),
            SimpleNamespace(type=WSMsgType.CLOSE, data=None),
        )
        session = FakeWebSession(socket)
        transport = RealtimeWebSocketTransport(
            session, "luma.local", 8080, limits()
        )
        await transport.async_connect()
        received = await transport.async_receive()
        self.assertIsInstance(received.value, RealtimeStreamStart)
        self.assertEqual(socket.pongs, [b"x"])
        self.assertEqual(session.calls[0][0], "ws://luma.local:8080/ws/realtime")
        self.assertEqual(session.calls[0][1]["max_msg_size"], 512)
        self.assertEqual(session.calls[0][1]["heartbeat"], 30.0)
        with self.assertRaises(RealtimeDisconnectedError):
            await transport.async_receive()
        await transport.async_close()
        self.assertTrue(socket.closed)
        self.assertFalse(session.closed)

    async def test_malformed_unknown_binary_and_oversize_frames_are_rejected(self):
        unknown = json.dumps({"type": "other"})
        unknown_kind = json.dumps({
            "type": "notification", "runtime_id": RUNTIME,
            "sequence": "1", "kind": "future_kind",
        })
        oversized = " " * 513
        cases = (
            (SimpleNamespace(type=WSMsgType.TEXT, data="{"), RealtimeProtocolError),
            (SimpleNamespace(type=WSMsgType.TEXT, data=unknown), RealtimeProtocolError),
            (SimpleNamespace(type=WSMsgType.TEXT, data=unknown_kind), RealtimeProtocolError),
            (SimpleNamespace(type=WSMsgType.BINARY, data=b"x"), RealtimeProtocolError),
            (SimpleNamespace(type=WSMsgType.ERROR, data=None), RealtimeDisconnectedError),
            (SimpleNamespace(type=WSMsgType.TEXT, data=oversized), RealtimeOverflowError),
        )
        for message, error in cases:
            with self.subTest(error=error):
                transport = RealtimeWebSocketTransport(
                    FakeWebSession(FakeSocket(message)), "luma.local",
                    limits=limits(),
                )
                await transport.async_connect()
                with self.assertRaises(error):
                    await transport.async_receive()
                await transport.async_close()

    async def test_receive_timeout_is_bounded(self):
        transport = RealtimeWebSocketTransport(
            FakeWebSession(FakeSocket(asyncio.TimeoutError())),
            "luma.local", limits=limits(),
        )
        await transport.async_connect()
        with self.assertRaises(asyncio.TimeoutError):
            await transport.async_receive()
        await transport.async_close()

    async def test_connect_timeout_is_bounded(self):
        transport = RealtimeWebSocketTransport(
            BlockingWebSession(), "luma.local",
            limits=limits(connect_timeout=0.01),
        )
        with self.assertRaises(RealtimeDisconnectedError):
            await transport.async_connect()


if __name__ == "__main__":
    unittest.main()
