"""Bounded Luma Realtime WebSocket client and coherent HTTP resync."""

import asyncio
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from enum import Enum
import inspect
from typing import Any

from aiohttp import ClientError, ClientSession, WSMsgType

from .api.client import _authority
from .api.errors import AquaOneError, AquaOneInvalidResponseError
from .api.realtime import (
    NotificationSequenceState,
    PositionComparison,
    RealtimeNotification,
    RealtimeStatusMetadata,
    RealtimeStreamPosition,
    RealtimeStreamStart,
    classify_notification,
    compare_realtime_positions,
    parse_realtime_message,
)
from .products.luma import LumaSnapshot, require_luma

MAX_WS_FRAME_BYTES = 512
MAX_OVERLAP_FRAMES = 32
MAX_OVERLAP_BYTES = 4096
CONNECT_TIMEOUT_SECONDS = 10.0
RECEIVE_TIMEOUT_SECONDS = 30.0
WEBSOCKET_HEARTBEAT_SECONDS = 30.0
RESYNC_TIMEOUT_SECONDS = 25.0
MAX_RESYNC_ATTEMPTS = 3
INITIAL_RECONNECT_BACKOFF_SECONDS = 1.0
MAX_RECONNECT_BACKOFF_SECONDS = 4.0


class RealtimeClientError(AquaOneError):
    """Base failure for one bounded Realtime session."""


class RealtimeProtocolError(RealtimeClientError):
    """The peer violated the public Realtime protocol."""


class RealtimeDisconnectedError(RealtimeClientError):
    """The WebSocket disconnected before a safe shutdown."""


class RealtimeOverflowError(RealtimeClientError):
    """A bounded Realtime buffer was exhausted."""


class RealtimeRunResult(Enum):
    STOPPED = "stopped"
    FALLBACK = "fallback"


class RealtimeLifecycleState(Enum):
    CONNECTING = "connecting"
    RESYNC = "resync"
    LIVE = "live"
    RECONNECT = "reconnect"
    FALLBACK = "fallback"
    SHUTDOWN = "shutdown"


@dataclass(frozen=True, slots=True)
class RealtimeClientLimits:
    max_frame_bytes: int = MAX_WS_FRAME_BYTES
    max_overlap_frames: int = MAX_OVERLAP_FRAMES
    max_overlap_bytes: int = MAX_OVERLAP_BYTES
    connect_timeout: float = CONNECT_TIMEOUT_SECONDS
    receive_timeout: float = RECEIVE_TIMEOUT_SECONDS
    resync_timeout: float = RESYNC_TIMEOUT_SECONDS
    max_resync_attempts: int = MAX_RESYNC_ATTEMPTS
    initial_backoff: float = INITIAL_RECONNECT_BACKOFF_SECONDS
    max_backoff: float = MAX_RECONNECT_BACKOFF_SECONDS

    def __post_init__(self) -> None:
        integer_limits = (
            self.max_frame_bytes,
            self.max_overlap_frames,
            self.max_overlap_bytes,
            self.max_resync_attempts,
        )
        time_limits = (
            self.connect_timeout,
            self.receive_timeout,
            self.resync_timeout,
            self.initial_backoff,
            self.max_backoff,
        )
        if any(type(value) is not int or value < 1 for value in integer_limits):
            raise ValueError("Realtime integer limits must be positive")
        if any(type(value) not in (int, float) or value <= 0 for value in time_limits):
            raise ValueError("Realtime time limits must be positive")
        if self.max_overlap_bytes < self.max_frame_bytes:
            raise ValueError("Overlap byte limit must fit one frame")
        if self.max_backoff < self.initial_backoff:
            raise ValueError("Maximum backoff is below initial backoff")


@dataclass(frozen=True, slots=True)
class ReceivedRealtimeMessage:
    value: RealtimeStreamStart | RealtimeNotification
    encoded_size: int


@dataclass(frozen=True, slots=True)
class RealtimeSyncState:
    installed_watermark: RealtimeStatusMetadata
    seen_position: RealtimeStatusMetadata


SnapshotCallback = Callable[[LumaSnapshot], Awaitable[None] | None]
FallbackCallback = Callable[[AquaOneError], Awaitable[None] | None]
LifecycleCallback = Callable[[RealtimeLifecycleState], Awaitable[None] | None]
ConnectGate = Callable[[], bool]


async def _invoke(callback: Callable[[Any], Awaitable[None] | None], value: Any) -> None:
    result = callback(value)
    if inspect.isawaitable(result):
        await result


async def _cancel(task: asyncio.Task[Any] | None) -> None:
    if task is None:
        return
    if task.done():
        try:
            task.result()
        except asyncio.CancelledError:
            pass
        except Exception:
            pass
        return
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass


class RealtimeWebSocketTransport:
    """One WebSocket connection using an injected, non-owned HA session."""

    def __init__(
        self,
        session: ClientSession,
        host: str,
        port: int = 80,
        limits: RealtimeClientLimits = RealtimeClientLimits(),
    ) -> None:
        self._session = session
        self._url = f"ws://{_authority(host, port)}/ws/realtime"
        self._limits = limits
        self._socket: Any = None

    @property
    def connected(self) -> bool:
        return self._socket is not None and not self._socket.closed

    async def async_connect(self) -> None:
        if self.connected:
            raise RuntimeError("Realtime WebSocket is already connected")
        try:
            async with asyncio.timeout(self._limits.connect_timeout):
                self._socket = await self._session.ws_connect(
                    self._url,
                    autoclose=True,
                    autoping=True,
                    heartbeat=WEBSOCKET_HEARTBEAT_SECONDS,
                    max_msg_size=self._limits.max_frame_bytes,
                )
        except asyncio.TimeoutError as exc:
            raise RealtimeDisconnectedError("Realtime connect timed out") from exc
        except (ClientError, OSError) as exc:
            raise RealtimeDisconnectedError("Realtime connect failed") from exc

    async def async_receive(self) -> ReceivedRealtimeMessage:
        if not self.connected:
            raise RealtimeDisconnectedError("Realtime WebSocket is not connected")
        while True:
            try:
                message = await self._socket.receive(
                    timeout=self._limits.receive_timeout
                )
            except asyncio.TimeoutError:
                raise
            except (ClientError, OSError, RuntimeError) as exc:
                raise RealtimeDisconnectedError("Realtime receive failed") from exc

            if message.type is WSMsgType.TEXT:
                if not isinstance(message.data, str):
                    raise RealtimeProtocolError("Invalid TEXT frame")
                encoded_size = len(message.data.encode("utf-8"))
                if encoded_size > self._limits.max_frame_bytes:
                    raise RealtimeOverflowError("Realtime frame is too large")
                try:
                    value = parse_realtime_message(message.data)
                except AquaOneInvalidResponseError as exc:
                    raise RealtimeProtocolError(str(exc)) from exc
                return ReceivedRealtimeMessage(value, encoded_size)
            if message.type is WSMsgType.PING:
                await self._socket.pong(message.data)
                continue
            if message.type is WSMsgType.PONG:
                continue
            if message.type in (
                WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.CLOSING
            ):
                raise RealtimeDisconnectedError(
                    "Realtime peer closed the connection"
                )
            if message.type is WSMsgType.BINARY:
                raise RealtimeProtocolError("Realtime binary frame is invalid")
            if message.type is WSMsgType.ERROR:
                raise RealtimeDisconnectedError("Realtime WebSocket failed")
            raise RealtimeProtocolError("Unsupported Realtime WebSocket frame")

    async def async_close(self) -> None:
        socket, self._socket = self._socket, None
        if socket is not None and not socket.closed:
            try:
                await socket.close()
            except (ClientError, OSError, RuntimeError):
                pass


class LumaRealtimeClient:
    """Run WS-first resync without owning integration lifecycle or polling."""

    def __init__(
        self,
        transport: RealtimeWebSocketTransport,
        adapter: Any,
        expected_device_key: str,
        install_snapshot: SnapshotCallback,
        fallback: FallbackCallback | None = None,
        lifecycle: LifecycleCallback | None = None,
        can_connect: ConnectGate | None = None,
        limits: RealtimeClientLimits = RealtimeClientLimits(),
        sleep: Callable[[float], Awaitable[None]] = asyncio.sleep,
    ) -> None:
        self._transport = transport
        self._adapter = adapter
        self._expected_device_key = expected_device_key
        self._install_snapshot = install_snapshot
        self._fallback = fallback
        self._lifecycle = lifecycle
        self._can_connect = can_connect
        self._limits = limits
        self._sleep = sleep
        self._stopping = False
        self._state: RealtimeSyncState | None = None

    @property
    def state(self) -> RealtimeSyncState | None:
        return self._state

    async def async_run(self) -> RealtimeRunResult:
        """Run bounded reconnect attempts until stopped or polling fallback."""
        backoff = self._limits.initial_backoff
        last_error: AquaOneError | None = None
        try:
            for attempt in range(self._limits.max_resync_attempts):
                if self._stopping:
                    return RealtimeRunResult.STOPPED
                if self._can_connect is not None and not self._can_connect():
                    await self._notify_lifecycle(RealtimeLifecycleState.FALLBACK)
                    return RealtimeRunResult.FALLBACK
                self._state = None
                try:
                    await self._notify_lifecycle(
                        RealtimeLifecycleState.CONNECTING
                        if attempt == 0 else RealtimeLifecycleState.RECONNECT
                    )
                    await self._transport.async_connect()
                    await self._notify_lifecycle(RealtimeLifecycleState.RESYNC)
                    await self._async_resync()
                    await self._notify_lifecycle(RealtimeLifecycleState.LIVE)
                    await self._async_live()
                    if self._stopping:
                        return RealtimeRunResult.STOPPED
                    raise RealtimeDisconnectedError("Realtime session ended")
                except asyncio.CancelledError:
                    raise
                except AquaOneError as exc:
                    last_error = exc
                except asyncio.TimeoutError:
                    last_error = RealtimeDisconnectedError("Realtime resync timed out")
                finally:
                    await self._transport.async_close()
                if self._stopping:
                    return RealtimeRunResult.STOPPED
                if attempt + 1 < self._limits.max_resync_attempts:
                    await self._notify_lifecycle(RealtimeLifecycleState.RECONNECT)
                    await self._sleep(backoff)
                    backoff = min(backoff * 2, self._limits.max_backoff)
        finally:
            await self._transport.async_close()

        if self._stopping:
            return RealtimeRunResult.STOPPED
        assert last_error is not None
        await self._notify_lifecycle(RealtimeLifecycleState.FALLBACK)
        if self._fallback is not None:
            await _invoke(self._fallback, last_error)
        return RealtimeRunResult.FALLBACK

    async def async_close(self) -> None:
        """Request shutdown and unblock a pending receive without closing HA's session."""
        if self._stopping:
            return
        self._stopping = True
        await self._notify_lifecycle(RealtimeLifecycleState.SHUTDOWN)
        await self._transport.async_close()

    async def async_request_resync(self) -> None:
        """Interrupt the current socket so the bounded run starts full resync."""
        if not self._stopping:
            await self._transport.async_close()

    def accept_external_snapshot(self, snapshot: LumaSnapshot) -> None:
        """Validate and track one coordinator safety-poll snapshot."""
        if self._state is None:
            raise RealtimeProtocolError("Realtime is not synchronized")
        installed = self._validate_snapshot(snapshot, self._state.seen_position)
        seen = self._state.seen_position
        if compare_realtime_positions(
            installed, seen
        ) is PositionComparison.NEWER:
            seen = installed
        self._state = RealtimeSyncState(installed, seen)

    async def _notify_lifecycle(self, state: RealtimeLifecycleState) -> None:
        if self._lifecycle is not None:
            await _invoke(self._lifecycle, state)

    async def _async_resync(self) -> None:
        first = await self._transport.async_receive()
        if not isinstance(first.value, RealtimeStreamStart):
            raise RealtimeProtocolError("First Realtime frame must be StreamStart")
        stream_start = first.value
        start = RealtimeStatusMetadata(stream_start.runtime_id, stream_start.position)
        overlap: list[RealtimeNotification] = []
        overlap_bytes = 0
        snapshot_task: asyncio.Task[LumaSnapshot] | None = None
        receive_task: asyncio.Task[ReceivedRealtimeMessage] | None = None
        try:
            async with asyncio.timeout(self._limits.resync_timeout):
                snapshot_task = asyncio.create_task(
                    self._adapter.async_get_snapshot(self._expected_device_key)
                )
                receive_task = asyncio.create_task(self._transport.async_receive())
                while not snapshot_task.done():
                    done, _ = await asyncio.wait(
                        (snapshot_task, receive_task),
                        return_when=asyncio.FIRST_COMPLETED,
                    )
                    if receive_task in done:
                        received = receive_task.result()
                        if not isinstance(received.value, RealtimeNotification):
                            raise RealtimeProtocolError(
                                "StreamStart is only valid as the first frame"
                            )
                        overlap.append(received.value)
                        overlap_bytes += received.encoded_size
                        if (len(overlap) > self._limits.max_overlap_frames or
                                overlap_bytes > self._limits.max_overlap_bytes):
                            raise RealtimeOverflowError("Realtime overlap buffer overflow")
                        receive_task = asyncio.create_task(
                            self._transport.async_receive()
                        )
                if receive_task.done():
                    received = receive_task.result()
                    if not isinstance(received.value, RealtimeNotification):
                        raise RealtimeProtocolError(
                            "StreamStart is only valid as the first frame"
                        )
                    overlap.append(received.value)
                    overlap_bytes += received.encoded_size
                    if (len(overlap) > self._limits.max_overlap_frames or
                            overlap_bytes > self._limits.max_overlap_bytes):
                        raise RealtimeOverflowError("Realtime overlap buffer overflow")
                snapshot = snapshot_task.result()
        finally:
            await _cancel(receive_task)
            await _cancel(snapshot_task)

        installed = self._validate_snapshot(snapshot, start)
        seen = installed
        for notification in overlap:
            classification = classify_notification(seen, notification)
            if classification is NotificationSequenceState.DIFFERENT_RUNTIME:
                raise RealtimeProtocolError("Realtime runtime changed during resync")
            if classification is NotificationSequenceState.DUPLICATE_OR_OLD:
                continue
            if classification is NotificationSequenceState.FORWARD_GAP:
                raise RealtimeProtocolError("Realtime sequence gap during resync")
            seen = RealtimeStatusMetadata(
                notification.runtime_id,
                RealtimeStreamPosition.at(notification.sequence),
            )

        await _invoke(self._install_snapshot, snapshot)
        self._state = RealtimeSyncState(installed, seen)

    def _validate_snapshot(
        self,
        snapshot: LumaSnapshot,
        minimum: RealtimeStatusMetadata,
    ) -> RealtimeStatusMetadata:
        require_luma(snapshot.system)
        if snapshot.system.device_key != self._expected_device_key:
            raise RealtimeProtocolError("Configured device identity changed")
        version = snapshot.system.api_protocol_version
        if version.major != 1 or version.minor < 1:
            raise RealtimeProtocolError("Realtime requires Luma API 1.1")
        metadata = snapshot.status.realtime
        if metadata is None:
            raise RealtimeProtocolError("Realtime HTTP metadata is missing")
        comparison = compare_realtime_positions(metadata, minimum)
        if comparison is PositionComparison.DIFFERENT_RUNTIME:
            raise RealtimeProtocolError("HTTP and WebSocket runtime mismatch")
        if comparison is PositionComparison.OLDER:
            raise RealtimeProtocolError("HTTP snapshot precedes WebSocket position")
        return metadata

    async def _async_fetch_and_install(
        self,
        minimum: RealtimeStatusMetadata,
    ) -> RealtimeStatusMetadata:
        snapshot = await self._adapter.async_get_snapshot(self._expected_device_key)
        metadata = self._validate_snapshot(snapshot, minimum)
        await _invoke(self._install_snapshot, snapshot)
        return metadata

    async def _async_live(self) -> None:
        assert self._state is not None
        receive_task: asyncio.Task[ReceivedRealtimeMessage] | None = asyncio.create_task(
            self._transport.async_receive()
        )
        refresh_task: asyncio.Task[RealtimeStatusMetadata] | None = None
        dirty = self._state.seen_position != self._state.installed_watermark
        if dirty:
            dirty = False
            refresh_task = asyncio.create_task(
                self._async_fetch_and_install(self._state.seen_position)
            )
        try:
            while not self._stopping:
                tasks = [receive_task]
                if refresh_task is not None:
                    tasks.append(refresh_task)
                done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)

                if receive_task in done:
                    try:
                        received = receive_task.result()
                    except asyncio.TimeoutError:
                        received = None
                    if received is not None:
                        if not isinstance(received.value, RealtimeNotification):
                            raise RealtimeProtocolError("Unexpected live StreamStart")
                        classification = classify_notification(
                            self._state.seen_position, received.value
                        )
                        if classification is NotificationSequenceState.DIFFERENT_RUNTIME:
                            raise RealtimeProtocolError("Realtime runtime changed")
                        if classification is NotificationSequenceState.FORWARD_GAP:
                            raise RealtimeProtocolError("Realtime sequence gap")
                        if classification is NotificationSequenceState.CONTIGUOUS:
                            self._state = RealtimeSyncState(
                                self._state.installed_watermark,
                                RealtimeStatusMetadata(
                                    received.value.runtime_id,
                                    RealtimeStreamPosition.at(
                                        received.value.sequence
                                    ),
                                ),
                            )
                            if refresh_task is None:
                                refresh_task = asyncio.create_task(
                                    self._async_fetch_and_install(
                                        self._state.seen_position
                                    )
                                )
                            else:
                                dirty = True
                    receive_task = asyncio.create_task(
                        self._transport.async_receive()
                    )

                if refresh_task is not None and refresh_task in done:
                    installed = refresh_task.result()
                    seen = self._state.seen_position
                    if compare_realtime_positions(
                        installed, seen
                    ) is PositionComparison.NEWER:
                        seen = installed
                    self._state = RealtimeSyncState(
                        installed, seen
                    )
                    refresh_task = None
                    if dirty:
                        dirty = False
                        refresh_task = asyncio.create_task(
                            self._async_fetch_and_install(self._state.seen_position)
                        )
        finally:
            await _cancel(receive_task)
            await _cancel(refresh_task)
