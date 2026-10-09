"""The 13 read-only Luma status sensors."""

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorEntityDescription
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .products.luma import DAY_STATES, HEALTH_STATES, MODES, TIME_VALIDITY, entity_unique_id


SENSORS = (
    SensorEntityDescription(
        key="mode", translation_key="mode", device_class=SensorDeviceClass.ENUM,
        options=list(MODES),
    ),
    SensorEntityDescription(key="active_profile", translation_key="active_profile"),
    SensorEntityDescription(
        key="day_state", translation_key="day_state", device_class=SensorDeviceClass.ENUM,
        options=list(DAY_STATES),
    ),
    SensorEntityDescription(
        key="time_valid", translation_key="time_valid", device_class=SensorDeviceClass.ENUM,
        options=list(TIME_VALIDITY),
    ),
    SensorEntityDescription(
        key="overall_health", translation_key="overall_health",
        device_class=SensorDeviceClass.ENUM, options=list(HEALTH_STATES),
    ),
    *(
        SensorEntityDescription(
            key=f"channel_{channel}_final_level",
            translation_key=f"channel_{channel}_final_level",
            native_unit_of_measurement="%",
        )
        for channel in range(1, 9)
    ),
)


async def async_setup_entry(hass, entry, async_add_entities) -> None:
    coordinator = entry.runtime_data.coordinator
    async_add_entities(LumaSensor(coordinator, description) for description in SENSORS)


class LumaSensor(CoordinatorEntity, SensorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator, description: SensorEntityDescription) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        system = coordinator.data.system
        self._attr_unique_id = entity_unique_id(system.device_key, description.key)
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, system.device_key)},
            manufacturer="aquaOne",
            model="Luma",
            name=system.device_name.strip() or "Luma",
            sw_version=system.firmware_version,
            hw_version=system.hardware_variant or None,
        )

    @property
    def native_value(self):
        status = self.coordinator.data.status
        key = self.entity_description.key
        if key == "mode":
            return status.mode
        if key == "active_profile":
            return status.active_profile
        if key == "day_state":
            return status.day_state
        if key == "time_valid":
            return status.time_validity
        if key == "overall_health":
            return status.overall_health
        # Fixed physical channel keys, never response ordering as identity.
        channel = int(key.split("_")[1])
        return status.final_levels[channel - 1]

    @property
    def extra_state_attributes(self):
        if self.entity_description.key == "active_profile":
            return {"profile_name": self.coordinator.data.status.active_profile_name}
        return None
