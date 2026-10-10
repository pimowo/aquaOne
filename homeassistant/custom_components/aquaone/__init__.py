"""aquaOne local, read-only Home Assistant integration."""

from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from homeassistant.config_entries import ConfigEntry
    from homeassistant.core import HomeAssistant

    from .api.client import AquaOneClient
    from .coordinator import LumaCoordinator
    from .products.luma import LumaAdapter


@dataclass(slots=True)
class AquaOneRuntimeData:
    client: "AquaOneClient"
    adapter: "LumaAdapter"
    coordinator: "LumaCoordinator"


PLATFORMS = ["sensor"]


async def async_setup_entry(hass: "HomeAssistant", entry: "ConfigEntry") -> bool:
    from homeassistant.helpers.aiohttp_client import async_get_clientsession

    from .api.client import AquaOneClient
    from .coordinator import LumaCoordinator
    from .products.luma import LumaAdapter

    session = async_get_clientsession(hass)
    client = AquaOneClient(session, entry.data["host"], entry.data["port"])
    adapter = LumaAdapter(client)
    expected_key = f"{entry.data['device_type']}:{entry.data['device_id']}"
    coordinator = LumaCoordinator(hass, adapter, expected_key, entry)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = AquaOneRuntimeData(client, adapter, coordinator)
    await coordinator.async_start_realtime(
        entry, session, entry.data["host"], entry.data["port"]
    )
    try:
        await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    except BaseException:
        await coordinator.async_shutdown_realtime()
        raise
    return True


async def async_unload_entry(hass: "HomeAssistant", entry: "ConfigEntry") -> bool:
    coordinator = entry.runtime_data.coordinator
    await coordinator.async_shutdown_realtime()
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if not unloaded:
        coordinator.resume_fallback_polling()
    return unloaded
