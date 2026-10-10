"""One authoritative Luma snapshot stream per config entry."""

import asyncio
from datetime import timedelta
import logging
from typing import Any

from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api.errors import AquaOneError, AquaOneInvalidResponseError
from .api.realtime import PositionComparison, compare_realtime_positions
from .products.luma import LumaAdapter, LumaSnapshot
from .realtime_client import (
    LumaRealtimeClient,
    RealtimeLifecycleState,
    RealtimeRunResult,
    RealtimeWebSocketTransport,
)

_LOGGER = logging.getLogger(__name__)
FALLBACK_POLL_INTERVAL = timedelta(seconds=10)
REALTIME_SAFETY_POLL_INTERVAL = timedelta(seconds=60)
REALTIME_RECONNECT_INTERVAL_SECONDS = 60


class LumaCoordinator(DataUpdateCoordinator[LumaSnapshot]):
    def __init__(
        self,
        hass,
        adapter: LumaAdapter,
        expected_device_key: str,
        config_entry=None,
    ) -> None:
        options: dict[str, Any] = {
            "name": "aquaOne Luma",
            "update_interval": FALLBACK_POLL_INTERVAL,
            "always_update": False,
        }
        if config_entry is not None:
            options["config_entry"] = config_entry
        super().__init__(
            hass,
            _LOGGER,
            **options,
        )
        self._adapter = adapter
        self._expected_device_key = expected_device_key
        self._config_entry = config_entry
        self._snapshot_lock = asyncio.Lock()
        self._realtime_client: LumaRealtimeClient | None = None
        self._realtime_task: asyncio.Task[RealtimeRunResult] | None = None
        self._poll_rearm_task: asyncio.Task[None] | None = None
        self._realtime_stopping = False
        self._realtime_live = False
        self._realtime_capability_blocked = False

    @property
    def realtime_active(self) -> bool:
        return self._realtime_task is not None and not self._realtime_task.done()

    async def async_get_snapshot(
        self, expected_device_key: str | None = None
    ) -> LumaSnapshot:
        """Serialize every HTTP cohort read for this ConfigEntry."""
        if (expected_device_key is not None and
                expected_device_key != self._expected_device_key):
            raise AquaOneInvalidResponseError("Configured device identity changed")
        async with self._snapshot_lock:
            return await self._adapter.async_get_snapshot(
                self._expected_device_key
            )

    async def _async_update_data(self) -> LumaSnapshot:
        try:
            snapshot = await self.async_get_snapshot()
            self._reject_stale_snapshot(snapshot)
            supports_realtime = self._snapshot_allows_realtime(snapshot)
            self._realtime_capability_blocked = not supports_realtime
            client = self._realtime_client
            if (self._realtime_live and client is not None and
                    client.state is not None):
                if supports_realtime:
                    client.accept_external_snapshot(snapshot)
                else:
                    await client.async_request_resync()
            return snapshot
        except AquaOneError as exc:
            self._realtime_capability_blocked = True
            client = self._realtime_client
            if (self._realtime_live and client is not None and
                    client.state is not None):
                await client.async_request_resync()
            raise UpdateFailed(str(exc)) from exc

    def _reject_stale_snapshot(self, snapshot: LumaSnapshot) -> None:
        current = getattr(self, "data", None)
        if current is None:
            return
        current_metadata = current.status.realtime
        new_metadata = snapshot.status.realtime
        if current_metadata is None or new_metadata is None:
            return
        if compare_realtime_positions(
            new_metadata, current_metadata
        ) is PositionComparison.OLDER:
            raise AquaOneInvalidResponseError("Stale Realtime HTTP snapshot")

    def _install_realtime_snapshot(self, snapshot: LumaSnapshot) -> None:
        if self._realtime_stopping:
            return
        self._reject_stale_snapshot(snapshot)
        self._realtime_capability_blocked = False
        self._set_poll_interval(REALTIME_SAFETY_POLL_INTERVAL)
        self.async_set_updated_data(snapshot)

    def _snapshot_allows_realtime(self, snapshot: LumaSnapshot | None) -> bool:
        if snapshot is None:
            return False
        system = snapshot.system
        return (
            system.device_key == self._expected_device_key
            and system.device_type == "luma"
            and system.api_protocol_version.major == 1
            and system.api_protocol_version.minor >= 1
            and snapshot.status.realtime is not None
        )

    def _current_snapshot_allows_realtime(self) -> bool:
        return bool(
            not self._realtime_capability_blocked
            and self.last_update_success
            and self._snapshot_allows_realtime(getattr(self, "data", None))
        )

    async def async_start_realtime(
        self,
        entry,
        session,
        host: str,
        port: int,
    ) -> bool:
        """Start one ConfigEntry-owned Realtime supervisor for API 1.1 Luma."""
        if self.realtime_active:
            return True
        if not self._current_snapshot_allows_realtime():
            return False

        self._realtime_stopping = False
        self._realtime_live = False
        transport = RealtimeWebSocketTransport(session, host, port)
        self._realtime_client = LumaRealtimeClient(
            transport,
            self,
            self._expected_device_key,
            self._install_realtime_snapshot,
            lifecycle=self._async_realtime_lifecycle,
            can_connect=self._current_snapshot_allows_realtime,
        )
        self._realtime_task = entry.async_create_background_task(
            self.hass,
            self._async_realtime_supervisor(),
            name="aquaOne Luma Realtime",
        )
        return True

    async def _async_realtime_supervisor(self) -> RealtimeRunResult:
        assert self._realtime_client is not None
        try:
            while not self._realtime_stopping:
                if not self._current_snapshot_allows_realtime():
                    self._realtime_live = False
                    if self._set_poll_interval(FALLBACK_POLL_INTERVAL):
                        self._schedule_poll_refresh()
                    await asyncio.sleep(REALTIME_RECONNECT_INTERVAL_SECONDS)
                    continue
                result = await self._realtime_client.async_run()
                if result is RealtimeRunResult.STOPPED:
                    return result
                await asyncio.sleep(REALTIME_RECONNECT_INTERVAL_SECONDS)
            return RealtimeRunResult.STOPPED
        except asyncio.CancelledError:
            self._realtime_stopping = True
            self._realtime_live = False
            _LOGGER.debug("Luma Realtime shutdown")
            raise
        finally:
            if not self._realtime_stopping:
                self._realtime_live = False
                if self._set_poll_interval(FALLBACK_POLL_INTERVAL):
                    self._schedule_poll_refresh()

    async def _async_realtime_lifecycle(
        self, state: RealtimeLifecycleState
    ) -> None:
        if self._realtime_stopping and state is not RealtimeLifecycleState.SHUTDOWN:
            return
        if state is RealtimeLifecycleState.LIVE:
            self._realtime_live = True
            _LOGGER.info("Luma Realtime live")
            self._set_poll_interval(REALTIME_SAFETY_POLL_INTERVAL)
            return
        if state is RealtimeLifecycleState.FALLBACK:
            self._realtime_live = False
            _LOGGER.info("Luma Realtime fallback to polling")
            if self._set_poll_interval(FALLBACK_POLL_INTERVAL):
                self._schedule_poll_refresh()
            return
        if state is RealtimeLifecycleState.SHUTDOWN:
            self._realtime_live = False
            _LOGGER.debug("Luma Realtime shutdown")
            return
        if state is RealtimeLifecycleState.CONNECTING:
            _LOGGER.debug("Luma Realtime connecting")
        elif state is RealtimeLifecycleState.RESYNC:
            _LOGGER.debug("Luma Realtime coherent resync")
        else:
            _LOGGER.debug("Luma Realtime reconnect")
        self._realtime_live = False
        if self._set_poll_interval(FALLBACK_POLL_INTERVAL):
            self._schedule_poll_refresh()

    def _set_poll_interval(self, interval: timedelta) -> bool:
        if self.update_interval == interval:
            return False
        self.update_interval = interval
        return True

    def _schedule_poll_refresh(self) -> None:
        """Rearm polling through one real coordinator refresh."""
        if self._realtime_stopping:
            return
        existing = self._poll_rearm_task
        if existing is not None and not existing.done():
            return
        coroutine = self._async_poll_refresh()
        if self._config_entry is not None:
            self._poll_rearm_task = (
                self._config_entry.async_create_background_task(
                    self.hass,
                    coroutine,
                    name="aquaOne Luma polling rearm",
                    eager_start=False,
                )
            )
        else:
            self._poll_rearm_task = asyncio.create_task(
                coroutine, name="aquaOne Luma polling rearm"
            )

    async def _async_poll_refresh(self) -> None:
        try:
            await self.async_request_refresh()
        finally:
            if self._poll_rearm_task is asyncio.current_task():
                self._poll_rearm_task = None

    async def async_shutdown_realtime(self) -> None:
        """Stop Realtime without closing the shared aiohttp session."""
        if self._realtime_stopping:
            return
        self._realtime_stopping = True
        self._realtime_live = False
        client, task = self._realtime_client, self._realtime_task
        poll_task = self._poll_rearm_task
        self._realtime_client = None
        self._realtime_task = None
        self._poll_rearm_task = None
        self.update_interval = FALLBACK_POLL_INTERVAL
        try:
            if poll_task is not None and poll_task is not asyncio.current_task():
                poll_task.cancel()
                try:
                    await poll_task
                except asyncio.CancelledError:
                    pass
            if client is not None:
                await client.async_close()
        finally:
            if task is not None and task is not asyncio.current_task():
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass

    def resume_fallback_polling(self) -> None:
        """Restore polling if platform unload was rejected."""
        self._realtime_stopping = False
        self._realtime_live = False
        self.update_interval = FALLBACK_POLL_INTERVAL
        self._schedule_poll_refresh()
