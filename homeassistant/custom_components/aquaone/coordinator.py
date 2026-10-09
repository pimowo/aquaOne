"""One atomic read-only Luma snapshot per config entry."""

from datetime import timedelta
import logging

from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api.errors import AquaOneError
from .products.luma import LumaAdapter, LumaSnapshot

_LOGGER = logging.getLogger(__name__)


class LumaCoordinator(DataUpdateCoordinator[LumaSnapshot]):
    def __init__(self, hass, adapter: LumaAdapter, expected_device_key: str) -> None:
        super().__init__(
            hass,
            _LOGGER,
            name="aquaOne Luma",
            update_interval=timedelta(seconds=10),  # Provisional; measure in F10.2B.
            always_update=False,
        )
        self._adapter = adapter
        self._expected_device_key = expected_device_key

    async def _async_update_data(self) -> LumaSnapshot:
        try:
            return await self._adapter.async_get_snapshot(self._expected_device_key)
        except AquaOneError as exc:
            raise UpdateFailed(str(exc)) from exc
