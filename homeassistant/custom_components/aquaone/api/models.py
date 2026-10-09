"""Validated, immutable common aquaOne API 1.x models."""

from dataclasses import dataclass, field
import re
from typing import Any

from .errors import AquaOneInvalidResponseError, AquaOneUnsupportedProtocolError

_TYPE = re.compile(r"[a-z][a-z0-9_-]{0,22}\Z")
_ID = re.compile(r"[0-9A-F]{12}\Z")


def _object(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AquaOneInvalidResponseError("Expected a JSON object")
    return value


def _string(data: dict[str, Any], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str):
        raise AquaOneInvalidResponseError(f"Invalid {key}")
    return value


def _integer(data: dict[str, Any], key: str, minimum: int = 0) -> int:
    value = data.get(key)
    if type(value) is not int or value < minimum:
        raise AquaOneInvalidResponseError(f"Invalid {key}")
    return value


@dataclass(frozen=True, slots=True)
class ApiProtocolVersion:
    major: int
    minor: int

    @classmethod
    def from_json(cls, value: Any) -> "ApiProtocolVersion":
        data = _object(value)
        major = _integer(data, "major")
        minor = _integer(data, "minor")
        if major != 1:
            raise AquaOneUnsupportedProtocolError("Unsupported API major version")
        return cls(major, minor)


@dataclass(frozen=True, slots=True)
class SystemInfo:
    device_type: str
    device_id: str
    api_protocol_version: ApiProtocolVersion
    device_type_legacy: str
    device_name: str
    firmware_version: str
    hardware_variant: str
    aqua_core_version: str
    uptime_ms: int = field(compare=False)
    restart_reason: str

    @property
    def device_key(self) -> str:
        return f"{self.device_type}:{self.device_id}"

    @classmethod
    def from_json(cls, value: Any) -> "SystemInfo":
        data = _object(value)
        device_type = _string(data, "device_type")
        device_id = _string(data, "device_id")
        if not _TYPE.fullmatch(device_type):
            raise AquaOneInvalidResponseError("Invalid device_type")
        if not _ID.fullmatch(device_id):
            raise AquaOneInvalidResponseError("Invalid device_id")
        return cls(
            device_type=device_type,
            device_id=device_id,
            api_protocol_version=ApiProtocolVersion.from_json(
                data.get("api_protocol_version")
            ),
            device_type_legacy=_string(data, "deviceType"),
            device_name=_string(data, "deviceName"),
            firmware_version=_string(data, "firmwareVersion"),
            hardware_variant=_string(data, "hardwareVariant"),
            aqua_core_version=_string(data, "aquaCoreVersion"),
            uptime_ms=_integer(data, "uptimeMs"),
            restart_reason=_string(data, "restartReason"),
        )
