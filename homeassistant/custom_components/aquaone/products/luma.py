"""Pure Luma API 1.x status validation and semantic mapping."""

from dataclasses import dataclass, field
from datetime import datetime
import math
import re
from typing import Any

from ..api.errors import AquaOneInvalidResponseError
from ..api.models import SystemInfo

MODES = ("normal", "service", "manual", "channel_test", "preview", "simulation", "off")
DAY_STATES = ("day", "night")
TIME_VALIDITY = ("valid", "invalid")
HEALTH_STATES = ("ok", "unknown", "warning", "error")
WIFI_STATES = ("disabled", "idle", "connecting", "connected", "disconnected", "error", "unknown")
_LOCAL_TIME = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\Z")


class AquaOneUnsupportedProductError(AquaOneInvalidResponseError):
    """A valid aquaOne product has no F10.2 adapter."""


def require_luma(system: SystemInfo) -> None:
    if system.device_type != "luma":
        raise AquaOneUnsupportedProductError("Unsupported aquaOne product")


def _mapping(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return value


def _string(data: dict[str, Any], field: str) -> str:
    value = data.get(field)
    if not isinstance(value, str):
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return value


def _enum(data: dict[str, Any], field: str, allowed: tuple[str, ...]) -> str:
    value = _string(data, field)
    if value not in allowed:
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return value.lower()


def _levels(data: dict[str, Any], field: str) -> tuple[float, ...]:
    value = data.get(field)
    if not isinstance(value, list) or len(value) != 8:
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return tuple(_percent(item, field) for item in value)


def _percent(value: Any, field: str) -> float:
    if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 100:
        raise AquaOneInvalidResponseError(f"Invalid {field}")
    return float(value)


@dataclass(frozen=True, slots=True)
class LumaStatus:
    mode: str
    active_profile: int
    active_profile_name: str
    day_state: str
    local_time: str = field(compare=False)
    requested_levels: tuple[float, ...] = field(compare=False)
    final_levels: tuple[float, ...]
    global_power_limit: float = field(compare=False)
    time_valid: bool
    wifi_connected: bool = field(compare=False)
    wifi_rssi: int = field(compare=False)
    wifi_state: str = field(compare=False)
    wifi_ip: str = field(compare=False)
    overall_health: str

    @property
    def time_validity(self) -> str:
        return "valid" if self.time_valid else "invalid"

    @classmethod
    def from_json(cls, value: Any) -> "LumaStatus":
        data = _mapping(value, "status")
        mode = _enum(data, "mode", tuple(item.upper() for item in MODES))
        day_state = _enum(data, "dayState", tuple(item.upper() for item in DAY_STATES))
        health = _enum(data, "overallHealth", HEALTH_STATES)
        profile = data.get("activeProfile")
        if type(profile) is not int or not 1 <= profile <= 5:
            raise AquaOneInvalidResponseError("Invalid activeProfile")
        local_time = _string(data, "localTime")
        if local_time != "invalid":
            if not _LOCAL_TIME.fullmatch(local_time):
                raise AquaOneInvalidResponseError("Invalid localTime")
            try:
                datetime.strptime(local_time, "%Y-%m-%d %H:%M:%S")
            except ValueError as exc:
                raise AquaOneInvalidResponseError("Invalid localTime") from exc
        time_valid = data.get("timeValid")
        if type(time_valid) is not bool:
            raise AquaOneInvalidResponseError("Invalid timeValid")
        wifi = _mapping(data.get("wifi"), "wifi")
        connected = wifi.get("connected")
        rssi = wifi.get("rssi")
        if type(connected) is not bool or type(rssi) is not int:
            raise AquaOneInvalidResponseError("Invalid wifi")
        return cls(
            mode=mode,
            active_profile=profile,
            active_profile_name=_string(data, "activeProfileName"),
            day_state=day_state,
            local_time=local_time,
            requested_levels=_levels(data, "requestedLevels"),
            final_levels=_levels(data, "finalLevels"),
            global_power_limit=_percent(data.get("globalPowerLimit"), "globalPowerLimit"),
            time_valid=time_valid,
            wifi_connected=connected,
            wifi_rssi=rssi,
            wifi_state=_enum(wifi, "state", WIFI_STATES),
            wifi_ip=_string(wifi, "ip"),
            overall_health=health,
        )


@dataclass(frozen=True, slots=True)
class LumaSnapshot:
    system: SystemInfo
    status: LumaStatus


class LumaAdapter:
    def __init__(self, client: Any) -> None:
        self._client = client

    async def async_get_snapshot(self, expected_device_key: str | None = None) -> LumaSnapshot:
        # Exactly two sequential GETs; publish only after both have been validated.
        system = await self._client.async_get_system()
        require_luma(system)
        if expected_device_key is not None and system.device_key != expected_device_key:
            raise AquaOneInvalidResponseError("Configured device identity changed")
        status = LumaStatus.from_json(
            await self._client.async_get_json("/api/lumasense/status")
        )
        return LumaSnapshot(system, status)


def entity_unique_id(device_key: str, entity_key: str) -> str:
    return f"{device_key}:{entity_key}"
