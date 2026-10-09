"""Pure API/adapter checks; no Home Assistant runtime or device required."""

import asyncio
import importlib
import json
import logging
from pathlib import Path
import sys
from types import ModuleType
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from aiohttp import ClientConnectionError

from custom_components.aquaone.api.client import AquaOneClient, MAX_RESPONSE_BYTES
from custom_components.aquaone.api.errors import (
    AquaOneAuthenticationError,
    AquaOneConnectionError,
    AquaOneDeviceRejectedError,
    AquaOneInvalidResponseError,
    AquaOneTimeoutError,
    AquaOneUnsupportedProtocolError,
)
from custom_components.aquaone.api.models import SystemInfo
from custom_components.aquaone.products.luma import (
    AquaOneUnsupportedProductError,
    LumaAdapter,
    LumaStatus,
    entity_unique_id,
    require_luma,
)


def system_payload(**changes):
    value = {
        "deviceType": "lighting-controller",
        "deviceName": "Living room",
        "firmwareVersion": "1.2.3",
        "hardwareVariant": "classic",
        "aquaCoreVersion": "0.6.2",
        "uptimeMs": 100,
        "restartReason": "power_on",
        "device_type": "luma",
        "device_id": "246F28A1B2C3",
        "api_protocol_version": {"major": 1, "minor": 0},
    }
    value.update(changes)
    return value


def status_payload(**changes):
    value = {
        "mode": "NORMAL",
        "activeProfile": 2,
        "activeProfileName": "Reef",
        "dayState": "DAY",
        "localTime": "2026-10-09 12:34:56",
        "requestedLevels": [10.0] * 8,
        "finalLevels": [float(i) for i in range(1, 9)],
        "globalPowerLimit": 90.0,
        "timeValid": True,
        "wifi": {"state": "connected", "connected": True, "ip": "192.0.2.1", "rssi": -50},
        "overallHealth": "warning",
    }
    value.update(changes)
    return value


class ModelTests(unittest.TestCase):
    def test_system_and_protocol(self):
        system = SystemInfo.from_json(system_payload())
        self.assertEqual(system.device_key, "luma:246F28A1B2C3")
        self.assertEqual(system.api_protocol_version.minor, 0)
        self.assertEqual(
            SystemInfo.from_json(system_payload(api_protocol_version={"major": 1, "minor": 9})).api_protocol_version.minor,
            9,
        )
        for major in (0, 2):
            with self.subTest(major=major), self.assertRaises(AquaOneUnsupportedProtocolError):
                SystemInfo.from_json(system_payload(api_protocol_version={"major": major, "minor": 0}))

    def test_rejects_invalid_common_fields(self):
        cases = [
            {"device_type": "Luma"}, {"device_type": "a" * 24},
            {"device_id": "246f28a1b2c3"}, {"device_id": "A1B2"},
            {"device_id": "ZZZZZZZZZZZZ"}, {"api_protocol_version": {"major": True, "minor": 0}},
            {"api_protocol_version": {"major": 1, "minor": False}},
            {"api_protocol_version": {"major": 1, "minor": -1}},
            {"firmwareVersion": None}, {"deviceName": None}, {"uptimeMs": True},
        ]
        for change in cases:
            with self.subTest(change=change), self.assertRaises(AquaOneInvalidResponseError):
                SystemInfo.from_json(system_payload(**change))
        payload = system_payload()
        del payload["device_id"]
        with self.assertRaises(AquaOneInvalidResponseError):
            SystemInfo.from_json(payload)

    def test_stable_identifiers(self):
        original = SystemInfo.from_json(system_payload())
        for change in ({"deviceName": "Other"}, {"firmwareVersion": "99"}):
            modified = SystemInfo.from_json(system_payload(**change))
            self.assertEqual(modified.device_key, original.device_key)
            self.assertEqual(entity_unique_id(modified.device_key, "mode"), "luma:246F28A1B2C3:mode")
        self.assertEqual(SystemInfo.from_json(system_payload(uptimeMs=999)), original)
        self.assertNotEqual(SystemInfo.from_json(system_payload(device_id="AABBCCDDEEFF")).device_key, original.device_key)
        self.assertNotEqual(SystemInfo.from_json(system_payload(device_type="hydro")).device_key, original.device_key)
        self.assertEqual(entity_unique_id(original.device_key, "channel_8_final_level"), "luma:246F28A1B2C3:channel_8_final_level")


class LumaTests(unittest.TestCase):
    def test_complete_status_mapping(self):
        status = LumaStatus.from_json(status_payload())
        self.assertEqual((status.mode, status.active_profile, status.active_profile_name), ("normal", 2, "Reef"))
        self.assertEqual((status.day_state, status.time_validity, status.overall_health), ("day", "valid", "warning"))
        self.assertEqual(status.final_levels, tuple(float(i) for i in range(1, 9)))
        self.assertEqual(len(status.requested_levels), 8)
        self.assertTrue(status.wifi_connected)
        self.assertEqual(LumaStatus.from_json(status_payload(localTime="2026-10-09 12:34:57")), status)
        self.assertEqual(LumaStatus.from_json(status_payload(timeValid=False, localTime="invalid", overallHealth="error")).time_validity, "invalid")

    def test_bad_status(self):
        cases = [
            {"finalLevels": [1] * 7}, {"finalLevels": [1] * 9},
            {"requestedLevels": [1] * 7}, {"requestedLevels": [1] * 9},
            {"finalLevels": [float("nan")] * 8}, {"finalLevels": [float("inf")] * 8},
            {"requestedLevels": [-1] * 8}, {"finalLevels": [101] * 8},
            {"globalPowerLimit": float("inf")}, {"globalPowerLimit": True},
            {"activeProfile": 0}, {"activeProfile": 6}, {"activeProfile": True},
            {"timeValid": 1}, {"wifi": None}, {"wifi": {"connected": "yes", "rssi": -50, "state": "connected", "ip": ""}},
            {"wifi": {"connected": True, "rssi": True, "state": "connected", "ip": ""}},
            {"mode": "UNKNOWN"}, {"mode": "normal"}, {"dayState": "DUSK"},
            {"dayState": "day"}, {"overallHealth": "failed"},
            {"localTime": 42}, {"localTime": "2026-99-99 12:00:00"},
        ]
        for change in cases:
            with self.subTest(change=change), self.assertRaises(AquaOneInvalidResponseError):
                LumaStatus.from_json(status_payload(**change))
        payload = status_payload()
        del payload["wifi"]
        with self.assertRaises(AquaOneInvalidResponseError):
            LumaStatus.from_json(payload)

    def test_product_type(self):
        require_luma(SystemInfo.from_json(system_payload()))
        for kind in ("hydro", "doser"):
            with self.subTest(kind=kind), self.assertRaises(AquaOneUnsupportedProductError):
                require_luma(SystemInfo.from_json(system_payload(device_type=kind)))


class FakeContent:
    def __init__(self, body, chunk_size=None):
        self.body = body
        self.read_limit = None
        self.position = 0
        self.chunk_size = chunk_size

    async def read(self, limit):
        self.read_limit = limit
        if self.chunk_size is not None:
            limit = min(limit, self.chunk_size)
        chunk = self.body[self.position:self.position + limit]
        self.position += len(chunk)
        return chunk


class FakeResponse:
    def __init__(self, status=200, body=b"{}", content_type="application/json; charset=utf-8"):
        self.status = status
        self.headers = {"Content-Type": content_type}
        self.content = FakeContent(body)

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        return False


class FakeSession:
    def __init__(self, *responses):
        self.responses = list(responses)
        self.urls = []

    def get(self, url, *, timeout, allow_redirects):
        assert allow_redirects is False
        self.urls.append(url)
        if not self.responses:
            raise AssertionError("Unexpected GET")
        result = self.responses.pop(0)
        if isinstance(result, BaseException):
            raise result
        return result


def response(value, status=200, content_type="application/json"):
    return FakeResponse(status, json.dumps(value).encode(), content_type)


class ClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_two_reads_and_host_independent_identity(self):
        for host, expected in (("luma.local", "luma.local:80"), ("192.0.2.10", "192.0.2.10:80"), ("2001:db8::1", "[2001:db8::1]:80")):
            with self.subTest(host=host):
                session = FakeSession(response(system_payload()), response(status_payload()))
                snapshot = await LumaAdapter(AquaOneClient(session, host)).async_get_snapshot()
                self.assertEqual(snapshot.system.device_key, "luma:246F28A1B2C3")
                self.assertEqual(session.urls, [f"http://{expected}/api/system", f"http://{expected}/api/lumasense/status"])

    async def test_adapter_identity_change_stops_before_status(self):
        session = FakeSession(response(system_payload(device_id="AABBCCDDEEFF")))
        with self.assertRaises(AquaOneInvalidResponseError):
            await LumaAdapter(AquaOneClient(session, "luma.local")).async_get_snapshot("luma:246F28A1B2C3")
        self.assertEqual(len(session.urls), 1)

    async def test_http_failures(self):
        for status, error in ((401, AquaOneAuthenticationError), (403, AquaOneAuthenticationError), (503, AquaOneDeviceRejectedError), (404, AquaOneDeviceRejectedError)):
            with self.subTest(status=status), self.assertRaises(error):
                await AquaOneClient(FakeSession(FakeResponse(status)), "luma.local").async_get_json("/api/system")
        for failure, error in ((ClientConnectionError("offline"), AquaOneConnectionError), (asyncio.TimeoutError(), AquaOneTimeoutError)):
            with self.subTest(error=error), self.assertRaises(error):
                await AquaOneClient(FakeSession(failure), "luma.local").async_get_json("/api/system")

    async def test_invalid_http_response(self):
        cases = [
            FakeResponse(body=b"<html>", content_type="text/html"),
            FakeResponse(body=b"{"),
            FakeResponse(body=b"[1]"),
            FakeResponse(body=b'{"bad": NaN}'),
            FakeResponse(body=b"x" * (MAX_RESPONSE_BYTES + 2)),
        ]
        for item in cases:
            with self.subTest(item=item), self.assertRaises(AquaOneInvalidResponseError):
                await AquaOneClient(FakeSession(item), "luma.local").async_get_json("/api/system")
            if len(item.content.body) > MAX_RESPONSE_BYTES:
                self.assertEqual(item.content.position, MAX_RESPONSE_BYTES + 1)

    async def test_partial_stream_reads_and_no_redirect(self):
        item = response(system_payload())
        item.content.chunk_size = 3
        session = FakeSession(item)
        system = await AquaOneClient(session, "luma.local", 8080).async_get_system()
        self.assertEqual(system.device_key, "luma:246F28A1B2C3")
        self.assertEqual(session.urls, ["http://luma.local:8080/api/system"])
        with self.assertRaises(AquaOneDeviceRejectedError):
            await AquaOneClient(FakeSession(FakeResponse(status=302)), "luma.local").async_get_system()

    async def test_host_and_paths_are_restricted(self):
        for host in ("http://luma.local", "luma.local/path", "user@luma.local", "luma.local:80", "[bad]", " luma.local"):
            with self.subTest(host=host), self.assertRaises(ValueError):
                AquaOneClient(FakeSession(), host)
        for port in (0, 65536, True):
            with self.subTest(port=port), self.assertRaises(ValueError):
                AquaOneClient(FakeSession(), "luma.local", port)
        with self.assertRaises(ValueError):
            await AquaOneClient(FakeSession(), "luma.local").async_get_json("/admin")


class CoordinatorContractTests(unittest.TestCase):
    def test_constructor_passes_required_logger(self):
        """A minimal HA constructor signature catches missing logger regressions."""
        ha = ModuleType("homeassistant")
        ha.__path__ = []
        helpers = ModuleType("homeassistant.helpers")
        helpers.__path__ = []
        update = ModuleType("homeassistant.helpers.update_coordinator")

        class DataUpdateCoordinator:
            def __class_getitem__(cls, _):
                return cls

            def __init__(self, hass, logger, *, name, update_interval, always_update):
                self.constructor_args = (hass, logger, name, update_interval, always_update)

        update.DataUpdateCoordinator = DataUpdateCoordinator
        update.UpdateFailed = type("UpdateFailed", (Exception,), {})
        modules = {
            "homeassistant": ha,
            "homeassistant.helpers": helpers,
            "homeassistant.helpers.update_coordinator": update,
        }
        try:
            with patch.dict(sys.modules, modules):
                coordinator_module = importlib.import_module("custom_components.aquaone.coordinator")
                hass, adapter = object(), object()
                coordinator = coordinator_module.LumaCoordinator(hass, adapter, "luma:246F28A1B2C3")
                received = coordinator.constructor_args
                self.assertIs(received[0], hass)
                self.assertIsInstance(received[1], logging.Logger)
                self.assertEqual(received[2], "aquaOne Luma")
                self.assertEqual(received[3].total_seconds(), 10)
                self.assertIs(received[4], False)
        finally:
            sys.modules.pop("custom_components.aquaone.coordinator", None)


if __name__ == "__main__":
    unittest.main()
