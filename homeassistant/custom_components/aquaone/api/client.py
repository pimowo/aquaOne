"""Bounded, read-only HTTP transport for current aquaOne devices."""

import asyncio
import ipaddress
import json
import re
from typing import Any

from aiohttp import ClientError, ClientSession, ClientTimeout

from .errors import (
    AquaOneAuthenticationError,
    AquaOneConnectionError,
    AquaOneDeviceRejectedError,
    AquaOneInvalidResponseError,
    AquaOneTimeoutError,
)
from .models import SystemInfo

# Provisional LAN client settings; F10.2B will measure them on real HA/device.
REQUEST_TIMEOUT_SECONDS = 10
MAX_RESPONSE_BYTES = 65536
_HOST_LABEL = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\Z")
_PATHS = frozenset({"/api/system", "/api/lumasense/status"})


def _reject_nonfinite(_: str) -> None:
    raise ValueError("Non-finite JSON number")


def _authority(host: str, port: int) -> str:
    if not isinstance(host, str) or not host or host != host.strip():
        raise ValueError("Invalid host")
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError("Invalid port")
    candidate = host[1:-1] if host.startswith("[") and host.endswith("]") else host
    try:
        address = ipaddress.ip_address(candidate)
    except ValueError:
        if len(host) > 253 or not all(_HOST_LABEL.fullmatch(part) for part in host.split(".")):
            raise ValueError("Invalid host") from None
        return f"{host}:{port}"
    if isinstance(address, ipaddress.IPv6Address):
        return f"[{address.compressed}]:{port}"
    return f"{address.compressed}:{port}"


class AquaOneClient:
    """Use an injected HA shared session; never own or close that session."""

    def __init__(self, session: ClientSession, host: str, port: int = 80) -> None:
        self._session = session
        self._base_url = f"http://{_authority(host, port)}"

    async def async_get_system(self) -> SystemInfo:
        return SystemInfo.from_json(await self.async_get_json("/api/system"))

    async def async_get_json(self, path: str) -> dict[str, Any]:
        if path not in _PATHS:
            raise ValueError("Unsupported read path")
        try:
            async with self._session.get(
                self._base_url + path,
                timeout=ClientTimeout(total=REQUEST_TIMEOUT_SECONDS),
                allow_redirects=False,
            ) as response:
                if response.status in (401, 403):
                    raise AquaOneAuthenticationError("Device denied access")
                if response.status != 200:
                    raise AquaOneDeviceRejectedError("Device rejected read request")
                content_type = response.headers.get("Content-Type", "")
                media_type = content_type.split(";", 1)[0].strip().lower()
                if media_type != "application/json":
                    raise AquaOneInvalidResponseError("Expected JSON content type")
                body = bytearray()
                while len(body) <= MAX_RESPONSE_BYTES:
                    chunk = await response.content.read(
                        min(8192, MAX_RESPONSE_BYTES + 1 - len(body))
                    )
                    if not chunk:
                        break
                    body.extend(chunk)
        except asyncio.TimeoutError as exc:
            raise AquaOneTimeoutError("Device request timed out") from exc
        except (ClientError, OSError) as exc:
            raise AquaOneConnectionError("Device connection failed") from exc
        if len(body) > MAX_RESPONSE_BYTES:
            raise AquaOneInvalidResponseError("JSON response too large")
        try:
            value = json.loads(body.decode("utf-8"), parse_constant=_reject_nonfinite)
        except (UnicodeError, ValueError) as exc:
            raise AquaOneInvalidResponseError("Malformed JSON response") from exc
        if not isinstance(value, dict):
            raise AquaOneInvalidResponseError("Expected JSON object")
        return value
