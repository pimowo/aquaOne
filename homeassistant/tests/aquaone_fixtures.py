"""Shared pure-test payload fixtures for the aquaOne integration."""


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
        "wifi": {
            "state": "connected", "connected": True,
            "ip": "192.0.2.1", "rssi": -50,
        },
        "overallHealth": "warning",
    }
    value.update(changes)
    return value
