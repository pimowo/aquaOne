"""Pure validation checks for the public aquaOne Realtime wire."""

from dataclasses import FrozenInstanceError
import importlib
import json
from pathlib import Path
import sys
from types import ModuleType
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from aquaone_fixtures import status_payload
from custom_components.aquaone.api.errors import AquaOneInvalidResponseError
from custom_components.aquaone.api.realtime import (
    UINT64_MAX,
    NotificationSequenceState,
    PositionComparison,
    RealtimeNotification,
    RealtimeStatusMetadata,
    RealtimeStreamPosition,
    RealtimeStreamStart,
    RuntimeIdentity,
    classify_notification,
    compare_realtime_positions,
    parse_realtime_message,
)
from custom_components.aquaone.products.luma import LumaStatus, entity_unique_id


RUNTIME = "0123456789ABCDEF"
OTHER_RUNTIME = "FEDCBA9876543210"


def metadata(runtime=RUNTIME, kind="at", sequence="1", **extra):
    position = {"kind": kind}
    if sequence is not None:
        position["sequence"] = sequence
    value = {"runtime_id": runtime, "position": position}
    value.update(extra)
    return value


class RealtimeModelTests(unittest.TestCase):
    def test_before_first_and_at_boundaries(self):
        before = RealtimeStreamPosition.from_json({"kind": "before_first"})
        at_one = RealtimeStreamPosition.from_json({"kind": "at", "sequence": "1"})
        at_max = RealtimeStreamPosition.from_json(
            {"kind": "at", "sequence": str(UINT64_MAX)}
        )
        self.assertEqual(before, RealtimeStreamPosition.before_first())
        self.assertEqual(at_one.sequence, 1)
        self.assertEqual(at_max.sequence, UINT64_MAX)
        with self.assertRaises(FrozenInstanceError):
            at_one.sequence = 2

    def test_runtime_identity_is_strict_and_nonzero(self):
        self.assertEqual(RuntimeIdentity(RUNTIME).value, RUNTIME)
        for value in (
            "0000000000000000", "0123456789abcDEF", "0123456789ABCDE",
            "0123456789ABCDEFG", "G123456789ABCDEF", 123,
        ):
            with self.subTest(value=value), self.assertRaises(AquaOneInvalidResponseError):
                RuntimeIdentity.from_json(value)

    def test_invalid_positions_and_sequences(self):
        cases = [
            {"kind": "before_first", "sequence": "1"},
            {"kind": "at"},
            {"kind": "at", "sequence": "0"},
            {"kind": "at", "sequence": "01"},
            {"kind": "at", "sequence": 1},
            {"kind": "at", "sequence": str(UINT64_MAX + 1)},
            {"kind": "later", "sequence": "1"},
            {"kind": 1},
        ]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(AquaOneInvalidResponseError):
                RealtimeStreamPosition.from_json(value)
        with self.assertRaises(AquaOneInvalidResponseError):
            RealtimeStatusMetadata(RUNTIME, RealtimeStreamPosition.before_first())
        with self.assertRaises(AquaOneInvalidResponseError):
            RealtimeStreamStart(RuntimeIdentity(RUNTIME), "before_first")


class RealtimeWireTests(unittest.TestCase):
    def test_exact_public_messages(self):
        before = parse_realtime_message(
            '{"type":"stream_start","runtime_id":"0123456789ABCDEF",'
            '"position":{"kind":"before_first"}}'
        )
        at_one = parse_realtime_message(
            '{"type":"stream_start","runtime_id":"0123456789ABCDEF",'
            '"position":{"kind":"at","sequence":"1"}}'
        )
        notification = parse_realtime_message(
            '{"type":"notification","runtime_id":"0123456789ABCDEF",'
            '"sequence":"1","kind":"luma_status_changed"}'
        )
        self.assertIsInstance(before, RealtimeStreamStart)
        self.assertEqual(before.position, RealtimeStreamPosition.before_first())
        self.assertEqual(at_one.position.sequence, 1)
        self.assertIsInstance(notification, RealtimeNotification)
        self.assertEqual(notification.sequence, 1)

    def test_unknown_optional_fields_are_ignored(self):
        message = json.dumps({
            "type": "stream_start", "runtime_id": RUNTIME,
            "position": {"kind": "at", "sequence": "1", "future": True},
            "future": {"value": 1},
        })
        parsed = parse_realtime_message(message)
        self.assertEqual(parsed.position.sequence, 1)
        notification = parse_realtime_message(json.dumps({
            "type": "notification", "runtime_id": RUNTIME,
            "sequence": "2", "kind": "luma_status_changed", "future": 7,
        }))
        self.assertEqual(notification.sequence, 2)

    def test_invalid_messages_are_rejected(self):
        cases = [
            b'{"type":"stream_start"}',
            "{", "[]", "null", '{"type":"other"}',
            json.dumps({"type": "stream_start", "runtime_id": RUNTIME}),
            json.dumps({
                "type": "stream_start", "runtime_id": "0000000000000000",
                "position": {"kind": "before_first"},
            }),
            json.dumps({
                "type": "notification", "runtime_id": RUNTIME,
                "sequence": "0", "kind": "luma_status_changed",
            }),
            json.dumps({
                "type": "notification", "runtime_id": RUNTIME,
                "sequence": "01", "kind": "luma_status_changed",
            }),
            json.dumps({
                "type": "notification", "runtime_id": RUNTIME,
                "sequence": 1, "kind": "luma_status_changed",
            }),
            json.dumps({
                "type": "notification", "runtime_id": RUNTIME,
                "sequence": "1", "kind": "unknown",
            }),
        ]
        for message in cases:
            with self.subTest(message=message), self.assertRaises(AquaOneInvalidResponseError):
                parse_realtime_message(message)


class StatusMetadataTests(unittest.TestCase):
    def test_api_1_0_status_without_metadata_remains_valid(self):
        status = LumaStatus.from_json(status_payload())
        self.assertIsNone(status.realtime)
        self.assertEqual((status.mode, status.active_profile), ("normal", 2))
        self.assertEqual(status.final_levels, tuple(float(i) for i in range(1, 9)))

    def test_optional_metadata_is_typed_and_allows_future_fields(self):
        value = metadata(future="ignored")
        value["position"]["future"] = "ignored"
        status = LumaStatus.from_json(status_payload(realtime=value))
        self.assertEqual(status.realtime.runtime_id, RuntimeIdentity(RUNTIME))
        self.assertEqual(status.realtime.position.sequence, 1)
        before = LumaStatus.from_json(status_payload(
            realtime=metadata(kind="before_first", sequence=None)
        ))
        self.assertEqual(before.realtime.position, RealtimeStreamPosition.before_first())

    def test_malformed_metadata_is_not_treated_as_absent(self):
        cases = [
            None,
            {},
            metadata(runtime="0000000000000000"),
            metadata(sequence="0"),
            metadata(sequence=1),
            metadata(kind="before_first", sequence="1"),
        ]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(AquaOneInvalidResponseError):
                LumaStatus.from_json(status_payload(realtime=value))


class SequenceComparisonTests(unittest.TestCase):
    @staticmethod
    def cursor(runtime=RUNTIME, sequence=None):
        position = (
            RealtimeStreamPosition.before_first()
            if sequence is None else RealtimeStreamPosition.at(sequence)
        )
        return RealtimeStatusMetadata(RuntimeIdentity(runtime), position)

    @staticmethod
    def notification(runtime=RUNTIME, sequence=1):
        return RealtimeNotification(
            RuntimeIdentity(runtime), sequence, "luma_status_changed"
        )

    def test_position_comparison(self):
        before = self.cursor()
        at_one = self.cursor(sequence=1)
        self.assertEqual(compare_realtime_positions(before, before), PositionComparison.EQUAL)
        self.assertEqual(compare_realtime_positions(before, at_one), PositionComparison.OLDER)
        self.assertEqual(compare_realtime_positions(at_one, before), PositionComparison.NEWER)
        self.assertEqual(
            compare_realtime_positions(at_one, self.cursor(OTHER_RUNTIME, 1)),
            PositionComparison.DIFFERENT_RUNTIME,
        )

    def test_notification_classification_and_no_wrap(self):
        self.assertEqual(
            classify_notification(self.cursor(), self.notification(sequence=1)),
            NotificationSequenceState.CONTIGUOUS,
        )
        self.assertEqual(
            classify_notification(self.cursor(), self.notification(sequence=2)),
            NotificationSequenceState.FORWARD_GAP,
        )
        for sequence in (1, 2):
            with self.subTest(sequence=sequence):
                self.assertEqual(
                    classify_notification(self.cursor(sequence=2), self.notification(sequence=sequence)),
                    NotificationSequenceState.DUPLICATE_OR_OLD,
                )
        self.assertEqual(
            classify_notification(self.cursor(sequence=1), self.notification(sequence=2)),
            NotificationSequenceState.CONTIGUOUS,
        )
        self.assertEqual(
            classify_notification(self.cursor(sequence=1), self.notification(sequence=3)),
            NotificationSequenceState.FORWARD_GAP,
        )
        self.assertEqual(
            classify_notification(self.cursor(sequence=1), self.notification(OTHER_RUNTIME, 2)),
            NotificationSequenceState.DIFFERENT_RUNTIME,
        )
        self.assertEqual(
            classify_notification(
                self.cursor(sequence=UINT64_MAX),
                self.notification(sequence=UINT64_MAX),
            ),
            NotificationSequenceState.DUPLICATE_OR_OLD,
        )
        with self.assertRaises(AquaOneInvalidResponseError):
            self.notification(sequence=0)


class EntityCompatibilityTests(unittest.TestCase):
    def test_existing_thirteen_sensor_keys_and_ids_are_unchanged(self):
        homeassistant = ModuleType("homeassistant")
        homeassistant.__path__ = []
        components = ModuleType("homeassistant.components")
        components.__path__ = []
        sensor = ModuleType("homeassistant.components.sensor")
        helpers = ModuleType("homeassistant.helpers")
        helpers.__path__ = []
        device_registry = ModuleType("homeassistant.helpers.device_registry")
        update_coordinator = ModuleType("homeassistant.helpers.update_coordinator")

        class SensorEntityDescription:
            def __init__(self, *, key, **kwargs):
                self.key = key

        class SensorDeviceClass:
            ENUM = "enum"

        class SensorEntity:
            pass

        class CoordinatorEntity:
            pass

        sensor.SensorEntityDescription = SensorEntityDescription
        sensor.SensorDeviceClass = SensorDeviceClass
        sensor.SensorEntity = SensorEntity
        device_registry.DeviceInfo = dict
        update_coordinator.CoordinatorEntity = CoordinatorEntity
        modules = {
            "homeassistant": homeassistant,
            "homeassistant.components": components,
            "homeassistant.components.sensor": sensor,
            "homeassistant.helpers": helpers,
            "homeassistant.helpers.device_registry": device_registry,
            "homeassistant.helpers.update_coordinator": update_coordinator,
        }
        try:
            with patch.dict(sys.modules, modules):
                module = importlib.import_module("custom_components.aquaone.sensor")
                keys = tuple(item.key for item in module.SENSORS)
        finally:
            sys.modules.pop("custom_components.aquaone.sensor", None)

        expected = (
            "mode", "active_profile", "day_state", "time_valid", "overall_health",
            *(f"channel_{channel}_final_level" for channel in range(1, 9)),
        )
        self.assertEqual(keys, expected)
        ids = tuple(entity_unique_id("luma:246F28A1B2C3", key) for key in keys)
        self.assertEqual(len(ids), 13)
        self.assertEqual(len(set(ids)), 13)


if __name__ == "__main__":
    unittest.main()
