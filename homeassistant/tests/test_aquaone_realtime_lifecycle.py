"""Deterministic HA lifecycle tests for the Luma Realtime coordinator."""

import asyncio
from collections import deque
from dataclasses import replace
from datetime import timedelta
import importlib
from pathlib import Path
import sys
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from aquaone_fixtures import status_payload, system_payload
from custom_components.aquaone.api.errors import AquaOneInvalidResponseError
from custom_components.aquaone.api.models import ApiProtocolVersion, SystemInfo
from custom_components.aquaone.products.luma import LumaSnapshot, LumaStatus
from custom_components.aquaone.realtime_client import (
    RealtimeLifecycleState,
    RealtimeRunResult,
)


class FakeUpdateFailed(Exception):
    pass


class FakeDataUpdateCoordinator:
    def __class_getitem__(cls, _):
        return cls

    def __init__(
        self, hass, logger, *, name, update_interval, always_update,
        config_entry=None,
    ):
        self.hass = hass
        self.logger = logger
        self.name = name
        self.update_interval = update_interval
        self.always_update = always_update
        self.config_entry = config_entry
        self.constructor_args = (
            hass, logger, name, update_interval, always_update
        )
        self.data = None
        self.last_update_success = False
        self.publications = []
        self.refresh_requests = 0
        self.refresh_active = 0
        self.max_refresh_active = 0
        self._refresh_lock = asyncio.Lock()

    async def async_config_entry_first_refresh(self):
        self.data = await self._async_update_data()
        self.last_update_success = True

    def async_set_updated_data(self, data):
        self.data = data
        self.last_update_success = True
        self.publications.append(data)

    async def async_request_refresh(self):
        self.refresh_requests += 1
        async with self._refresh_lock:
            self.refresh_active += 1
            self.max_refresh_active = max(
                self.max_refresh_active, self.refresh_active
            )
            try:
                try:
                    data = await self._async_update_data()
                except FakeUpdateFailed:
                    self.last_update_success = False
                    return
                self.async_set_updated_data(data)
            finally:
                self.refresh_active -= 1


ha = ModuleType("homeassistant")
ha.__path__ = []
helpers = ModuleType("homeassistant.helpers")
helpers.__path__ = []
update = ModuleType("homeassistant.helpers.update_coordinator")
update.DataUpdateCoordinator = FakeDataUpdateCoordinator
update.UpdateFailed = FakeUpdateFailed
sys.modules.setdefault("homeassistant", ha)
sys.modules.setdefault("homeassistant.helpers", helpers)
sys.modules.setdefault("homeassistant.helpers.update_coordinator", update)

import custom_components.aquaone as aquaone_package
from custom_components.aquaone import async_setup_entry, async_unload_entry

sys.modules.pop("custom_components.aquaone.coordinator", None)
if hasattr(aquaone_package, "coordinator"):
    delattr(aquaone_package, "coordinator")
coordinator_module = importlib.import_module(
    "custom_components.aquaone.coordinator"
)


RUNTIME = "0123456789ABCDEF"
OTHER_RUNTIME = "FEDCBA9876543210"
DEVICE_KEY = "luma:246F28A1B2C3"


def snapshot(sequence=None, runtime=RUNTIME, minor=1):
    position = {"kind": "before_first"}
    if sequence is not None:
        position = {"kind": "at", "sequence": str(sequence)}
    return LumaSnapshot(
        SystemInfo.from_json(system_payload(
            api_protocol_version={"major": 1, "minor": minor}
        )),
        LumaStatus.from_json(status_payload(realtime={
            "runtime_id": runtime,
            "position": position,
        })),
    )


def plain_snapshot(minor=1):
    return LumaSnapshot(
        SystemInfo.from_json(system_payload(
            api_protocol_version={"major": 1, "minor": minor}
        )),
        LumaStatus.from_json(status_payload()),
    )


class FakeAdapter:
    def __init__(self, *responses):
        self.responses = deque(responses)
        self.active = 0
        self.max_active = 0
        self.cancelled = False

    async def async_get_snapshot(self, expected_device_key=None):
        self.active += 1
        self.max_active = max(self.max_active, self.active)
        try:
            if expected_device_key not in (None, DEVICE_KEY):
                raise AssertionError("wrong identity")
            if not self.responses:
                raise AssertionError("unexpected snapshot request")
            result = self.responses.popleft()
            if isinstance(result, BaseException):
                raise result
            if callable(result):
                result = result()
            if hasattr(result, "__await__"):
                result = await result
            return result
        except asyncio.CancelledError:
            self.cancelled = True
            raise
        finally:
            self.active -= 1


class FakeEntry:
    def __init__(self, minor=1):
        self.data = {
            "host": "luma.local",
            "port": 80,
            "device_type": "luma",
            "device_id": "246F28A1B2C3",
        }
        self.minor = minor
        self.tasks = []
        self.runtime_data = None

    def async_create_background_task(
        self, hass, coroutine, name, eager_start=True
    ):
        task = asyncio.create_task(coroutine, name=name)
        self.tasks.append(task)
        return task


class FakeHass:
    def __init__(self, session=None):
        self.session = session or SimpleNamespace(closed=False)
        self.config_entries = SimpleNamespace()


class FakeTransport:
    instances = []

    def __init__(self, session, host, port):
        self.session = session
        self.host = host
        self.port = port
        self.__class__.instances.append(self)


class ControlledRealtimeClient:
    instances = []
    plan = "live"

    def __init__(
        self, transport, source, expected_key, install_snapshot,
        fallback=None, lifecycle=None, **_,
    ):
        self.transport = transport
        self.source = source
        self.expected_key = expected_key
        self.install_snapshot = install_snapshot
        self.lifecycle = lifecycle
        self.closed = False
        self.stop = asyncio.Event()
        self.run_count = 0
        self.state = None
        self.__class__.instances.append(self)

    async def async_run(self):
        self.run_count += 1
        try:
            if self.plan == "fallback_then_live" and self.run_count == 1:
                await self.lifecycle(RealtimeLifecycleState.FALLBACK)
                return RealtimeRunResult.FALLBACK
            await self.lifecycle(RealtimeLifecycleState.CONNECTING)
            await self.lifecycle(RealtimeLifecycleState.RESYNC)
            value = await self.source.async_get_snapshot(self.expected_key)
            self.install_snapshot(value)
            await self.lifecycle(RealtimeLifecycleState.LIVE)
            await self.stop.wait()
            return RealtimeRunResult.STOPPED
        finally:
            if asyncio.current_task().cancelling():
                self.closed = True

    async def async_close(self):
        self.closed = True
        self.stop.set()
        await self.lifecycle(RealtimeLifecycleState.SHUTDOWN)


async def wait_for(predicate, timeout=0.5):
    async with asyncio.timeout(timeout):
        while not predicate():
            await asyncio.sleep(0)


class CoordinatorLifecycleTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        FakeTransport.instances.clear()
        ControlledRealtimeClient.instances.clear()
        ControlledRealtimeClient.plan = "live"

    async def make_coordinator(self, adapter, initial, minor=1):
        entry = FakeEntry(minor)
        hass = FakeHass()
        coordinator = coordinator_module.LumaCoordinator(
            hass, adapter, DEVICE_KEY, entry
        )
        coordinator.data = initial
        coordinator.last_update_success = True
        return coordinator, hass, entry

    async def start(self, coordinator, hass, entry):
        patches = (
            patch.object(
                coordinator_module, "RealtimeWebSocketTransport", FakeTransport
            ),
            patch.object(
                coordinator_module, "LumaRealtimeClient", ControlledRealtimeClient
            ),
        )
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        return await coordinator.async_start_realtime(
            entry, hass.session, "luma.local", 80
        )

    async def test_api_10_is_polling_only(self):
        coordinator, hass, entry = await self.make_coordinator(
            FakeAdapter(), snapshot(minor=0), minor=0
        )
        self.assertFalse(await self.start(coordinator, hass, entry))
        self.assertFalse(coordinator.realtime_active)
        self.assertEqual(coordinator.update_interval, timedelta(seconds=10))
        self.assertEqual(entry.tasks, [])

    async def test_api_11_without_coherent_metadata_is_polling_only(self):
        coordinator, hass, entry = await self.make_coordinator(
            FakeAdapter(), plain_snapshot()
        )
        self.assertFalse(await self.start(coordinator, hass, entry))
        self.assertFalse(coordinator.realtime_active)
        self.assertEqual(entry.tasks, [])

    async def test_api_11_starts_one_task_and_first_sync_sets_safety_poll(self):
        adapter = FakeAdapter(snapshot(1))
        coordinator, hass, entry = await self.make_coordinator(
            adapter, snapshot()
        )
        self.assertTrue(await self.start(coordinator, hass, entry))
        first_task = entry.tasks[0]
        self.assertTrue(await coordinator.async_start_realtime(
            entry, hass.session, "luma.local", 80
        ))
        self.assertEqual(entry.tasks, [first_task])
        await wait_for(
            lambda: coordinator.update_interval == timedelta(seconds=60)
        )
        self.assertEqual(coordinator.data.status.realtime.position.sequence, 1)
        self.assertEqual(len(coordinator.publications), 1)
        self.assertEqual(len(FakeTransport.instances), 1)
        self.assertIs(FakeTransport.instances[0].session, hass.session)
        await coordinator.async_shutdown_realtime()
        self.assertFalse(hass.session.closed)

    async def test_failed_http_and_interval_changes_do_not_publish_cached_data(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(AquaOneInvalidResponseError("offline")), snapshot()
        )
        await coordinator.async_request_refresh()
        self.assertFalse(coordinator.last_update_success)
        self.assertEqual(coordinator.publications, [])

        self.assertTrue(coordinator._set_poll_interval(timedelta(seconds=60)))
        self.assertFalse(coordinator.last_update_success)
        self.assertEqual(coordinator.publications, [])

        self.assertTrue(coordinator._set_poll_interval(timedelta(seconds=10)))
        self.assertFalse(coordinator.last_update_success)
        self.assertEqual(coordinator.publications, [])

    async def test_fallback_rearm_waits_for_real_serialized_http_success(self):
        release = asyncio.Event()

        async def delayed():
            await release.wait()
            return snapshot(1)

        adapter = FakeAdapter(
            AquaOneInvalidResponseError("offline"), delayed, snapshot(2)
        )
        coordinator, _, entry = await self.make_coordinator(adapter, snapshot())
        await coordinator.async_request_refresh()
        self.assertFalse(coordinator.last_update_success)
        coordinator._set_poll_interval(timedelta(seconds=60))

        in_flight = asyncio.create_task(coordinator.async_request_refresh())
        await wait_for(lambda: adapter.active == 1)
        await coordinator._async_realtime_lifecycle(
            RealtimeLifecycleState.RECONNECT
        )
        await coordinator._async_realtime_lifecycle(
            RealtimeLifecycleState.RECONNECT
        )
        self.assertEqual(coordinator.update_interval, timedelta(seconds=10))
        self.assertFalse(coordinator.last_update_success)
        self.assertEqual(coordinator.publications, [])
        self.assertEqual(len(entry.tasks), 1)

        release.set()
        await in_flight
        await wait_for(lambda: coordinator._poll_rearm_task is None)
        self.assertTrue(coordinator.last_update_success)
        self.assertEqual(len(coordinator.publications), 2)
        self.assertEqual(adapter.max_active, 1)
        self.assertEqual(coordinator.max_refresh_active, 1)

    async def test_live_transition_has_one_real_snapshot_publication(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(), snapshot()
        )
        coordinator._install_realtime_snapshot(snapshot(1))
        await coordinator._async_realtime_lifecycle(RealtimeLifecycleState.LIVE)
        self.assertEqual(coordinator.update_interval, timedelta(seconds=60))
        self.assertEqual(coordinator.publications, [snapshot(1)])

    async def test_notification_publication_and_safety_poll_use_coordinator(self):
        adapter = FakeAdapter(snapshot(1), snapshot(2))
        coordinator, hass, entry = await self.make_coordinator(
            adapter, snapshot()
        )
        await self.start(coordinator, hass, entry)
        await wait_for(
            lambda: coordinator.data.status.realtime.position.sequence == 1
        )
        safety = await coordinator._async_update_data()
        coordinator.async_set_updated_data(safety)
        self.assertEqual(coordinator.data.status.realtime.position.sequence, 2)
        self.assertEqual(coordinator.update_interval, timedelta(seconds=60))
        await coordinator.async_shutdown_realtime()

    async def test_snapshot_lock_prevents_parallel_http(self):
        release = asyncio.Event()

        async def delayed():
            await release.wait()
            return snapshot(1)

        adapter = FakeAdapter(delayed, snapshot(2))
        coordinator, _, _ = await self.make_coordinator(adapter, snapshot())
        first = asyncio.create_task(coordinator.async_get_snapshot())
        await wait_for(lambda: adapter.active == 1)
        second = asyncio.create_task(coordinator.async_get_snapshot())
        await asyncio.sleep(0)
        self.assertEqual(adapter.max_active, 1)
        release.set()
        await asyncio.gather(first, second)
        self.assertEqual(adapter.max_active, 1)

    async def test_stale_snapshot_rejected_and_new_runtime_accepted(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(), snapshot(2)
        )
        with self.assertRaises(AquaOneInvalidResponseError):
            coordinator._install_realtime_snapshot(snapshot(1))
        coordinator._install_realtime_snapshot(
            snapshot(runtime=OTHER_RUNTIME)
        )
        self.assertEqual(
            coordinator.data.status.realtime.runtime_id.value, OTHER_RUNTIME
        )

    async def test_invalid_safety_snapshot_requests_full_resync(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(snapshot(1)), snapshot()
        )

        class Client:
            state = object()
            interrupted = False

            def accept_external_snapshot(self, value):
                raise AquaOneInvalidResponseError("runtime mismatch")

            async def async_request_resync(self):
                self.interrupted = True

        client = Client()
        coordinator._realtime_client = client
        coordinator._realtime_live = True
        with self.assertRaises(FakeUpdateFailed):
            await coordinator._async_update_data()
        self.assertTrue(client.interrupted)

    async def test_safety_poll_downgrade_publishes_http_but_blocks_ws(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(plain_snapshot(minor=0)), snapshot()
        )

        class Client:
            state = object()
            interrupted = False

            def accept_external_snapshot(self, value):
                raise AssertionError("API 1.0 must not enter Realtime validation")

            async def async_request_resync(self):
                self.interrupted = True

        client = Client()
        coordinator._realtime_client = client
        coordinator._realtime_live = True
        await coordinator.async_request_refresh()
        self.assertTrue(coordinator.last_update_success)
        self.assertEqual(
            coordinator.data.system.api_protocol_version.minor, 0
        )
        self.assertTrue(client.interrupted)
        self.assertFalse(coordinator._current_snapshot_allows_realtime())

    async def test_fallback_poll_accepts_new_runtime_without_old_ws_state(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(snapshot(runtime=OTHER_RUNTIME)), snapshot(2)
        )

        class Client:
            state = object()

            def accept_external_snapshot(self, value):
                raise AssertionError("fallback must not use stale WS state")

        coordinator._realtime_client = Client()
        coordinator._realtime_live = False
        result = await coordinator._async_update_data()
        self.assertEqual(
            result.status.realtime.runtime_id.value, OTHER_RUNTIME
        )

    async def test_fallback_polling_then_bounded_reconnect_returns_live(self):
        ControlledRealtimeClient.plan = "fallback_then_live"
        adapter = FakeAdapter(snapshot(1))
        coordinator, hass, entry = await self.make_coordinator(
            adapter, snapshot()
        )
        with patch.object(
            coordinator_module, "REALTIME_RECONNECT_INTERVAL_SECONDS", 0
        ):
            await self.start(coordinator, hass, entry)
            await wait_for(
                lambda: ControlledRealtimeClient.instances
                and ControlledRealtimeClient.instances[0].run_count == 2
                and coordinator.update_interval == timedelta(seconds=60)
            )
        self.assertEqual(len(entry.tasks), 1)
        await coordinator.async_shutdown_realtime()

    async def test_downgrade_blocks_cycles_until_verified_api_11_returns(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(), snapshot()
        )

        class Client:
            state = None

            def __init__(self):
                self.run_count = 0

            async def async_run(inner_self):
                inner_self.run_count += 1
                if inner_self.run_count == 1:
                    coordinator.async_set_updated_data(snapshot(minor=0))
                    return RealtimeRunResult.FALLBACK
                return RealtimeRunResult.STOPPED

        client = Client()
        coordinator._realtime_client = client
        original_gate = coordinator._current_snapshot_allows_realtime
        gate_checks = 0

        def controlled_gate():
            nonlocal gate_checks
            gate_checks += 1
            if gate_checks == 5:
                coordinator.async_set_updated_data(snapshot())
            return original_gate()

        coordinator._current_snapshot_allows_realtime = controlled_gate
        with patch.object(
            coordinator_module, "REALTIME_RECONNECT_INTERVAL_SECONDS", 0
        ):
            result = await coordinator._async_realtime_supervisor()
        self.assertIs(result, RealtimeRunResult.STOPPED)
        self.assertEqual(client.run_count, 2)
        self.assertGreaterEqual(gate_checks, 5)

    async def test_ineligible_or_unverified_snapshot_blocks_ws_attempts(self):
        coordinator, _, _ = await self.make_coordinator(
            FakeAdapter(), snapshot(minor=0)
        )

        class Client:
            state = None
            run_count = 0

            async def async_run(inner_self):
                inner_self.run_count += 1
                return RealtimeRunResult.STOPPED

        client = Client()
        coordinator._realtime_client = client
        original_gate = coordinator._current_snapshot_allows_realtime
        gate_checks = 0

        def stop_after_several_checks():
            nonlocal gate_checks
            gate_checks += 1
            if gate_checks == 4:
                coordinator._realtime_stopping = True
            return original_gate()

        coordinator._current_snapshot_allows_realtime = stop_after_several_checks
        with patch.object(
            coordinator_module, "REALTIME_RECONNECT_INTERVAL_SECONDS", 0
        ):
            result = await coordinator._async_realtime_supervisor()
        self.assertIs(result, RealtimeRunResult.STOPPED)
        self.assertEqual(client.run_count, 0)
        self.assertEqual(gate_checks, 4)

        coordinator._realtime_stopping = False
        cases = (
            plain_snapshot(),
            LumaSnapshot(
                replace(
                    snapshot().system,
                    api_protocol_version=ApiProtocolVersion(2, 0),
                ),
                snapshot().status,
            ),
            LumaSnapshot(
                SystemInfo.from_json(system_payload(device_id="AABBCCDDEEFF")),
                snapshot().status,
            ),
            LumaSnapshot(
                SystemInfo.from_json(system_payload(device_type="hydro")),
                snapshot().status,
            ),
        )
        for value in cases:
            with self.subTest(system=value.system):
                coordinator.data = value
                coordinator.last_update_success = True
                self.assertFalse(original_gate())

        unverified, _, _ = await self.make_coordinator(
            FakeAdapter(AquaOneInvalidResponseError("offline")), snapshot()
        )
        await unverified.async_request_refresh()
        self.assertFalse(unverified.last_update_success)
        self.assertFalse(unverified._current_snapshot_allows_realtime())

    async def test_unload_during_http_cancels_task_and_no_callback_survives(self):
        async def blocked():
            await asyncio.Future()

        adapter = FakeAdapter(blocked)
        coordinator, hass, entry = await self.make_coordinator(
            adapter, snapshot()
        )
        await self.start(coordinator, hass, entry)
        await wait_for(lambda: adapter.active == 1)
        publications = len(coordinator.publications)
        await coordinator.async_shutdown_realtime()
        self.assertTrue(adapter.cancelled)
        self.assertFalse(coordinator.realtime_active)
        self.assertEqual(len(coordinator.publications), publications)

    async def test_unload_cancels_pending_poll_rearm(self):
        async def blocked():
            await asyncio.Future()

        adapter = FakeAdapter(blocked)
        coordinator, _, _ = await self.make_coordinator(adapter, snapshot())
        coordinator._set_poll_interval(timedelta(seconds=60))
        await coordinator._async_realtime_lifecycle(
            RealtimeLifecycleState.RECONNECT
        )
        await wait_for(lambda: adapter.active == 1)
        await coordinator.async_shutdown_realtime()
        self.assertTrue(adapter.cancelled)
        self.assertIsNone(coordinator._poll_rearm_task)
        self.assertEqual(coordinator.publications, [])

    async def test_ha_shutdown_cancels_entry_owned_task(self):
        async def blocked():
            await asyncio.Future()

        adapter = FakeAdapter(blocked)
        coordinator, hass, entry = await self.make_coordinator(
            adapter, snapshot()
        )
        await self.start(coordinator, hass, entry)
        await wait_for(lambda: adapter.active == 1)
        entry.tasks[0].cancel()
        with self.assertRaises(asyncio.CancelledError):
            await entry.tasks[0]
        self.assertTrue(adapter.cancelled)
        self.assertTrue(coordinator._realtime_stopping)
        self.assertTrue(ControlledRealtimeClient.instances[0].closed)


class IntegrationSetupTests(unittest.IsolatedAsyncioTestCase):
    async def test_setup_failure_cleans_realtime_and_unload_stops_it(self):
        session = SimpleNamespace(closed=False)
        hass = FakeHass(session)
        entry = FakeEntry()
        coordinators = []

        class Client:
            def __init__(self, used_session, host, port):
                self.session = used_session

        class Adapter:
            def __init__(self, client):
                self.client = client

        class Coordinator:
            def __init__(self, used_hass, adapter, key, config_entry):
                self.started = False
                self.shutdown = False
                coordinators.append(self)

            async def async_config_entry_first_refresh(self):
                pass

            async def async_start_realtime(self, entry, used_session, host, port):
                self.started = True
                self.session = used_session
                return True

            async def async_shutdown_realtime(self):
                self.shutdown = True

            def resume_fallback_polling(self):
                self.resumed = True

        aiohttp_client = ModuleType("homeassistant.helpers.aiohttp_client")
        aiohttp_client.async_get_clientsession = lambda _: session
        hass.config_entries.async_forward_entry_setups = (
            lambda *_: self._raise_setup_failure()
        )
        hass.config_entries.async_unload_platforms = self._successful_unload
        with (
            patch.dict(sys.modules, {
                "homeassistant.helpers.aiohttp_client": aiohttp_client,
            }),
            patch("custom_components.aquaone.api.client.AquaOneClient", Client),
            patch("custom_components.aquaone.products.luma.LumaAdapter", Adapter),
            patch("custom_components.aquaone.coordinator.LumaCoordinator", Coordinator),
        ):
            with self.assertRaises(RuntimeError):
                await async_setup_entry(hass, entry)
            self.assertTrue(coordinators[0].started)
            self.assertTrue(coordinators[0].shutdown)
            self.assertIs(coordinators[0].session, session)

            coordinators[0].shutdown = False
            entry.runtime_data = SimpleNamespace(coordinator=coordinators[0])
            self.assertTrue(await async_unload_entry(hass, entry))
            self.assertTrue(coordinators[0].shutdown)
        self.assertFalse(session.closed)

    @staticmethod
    async def _raise_setup_failure():
        raise RuntimeError("platform setup failed")

    @staticmethod
    async def _successful_unload(*_):
        return True


if __name__ == "__main__":
    unittest.main()
