#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

constexpr size_t PUMP_COUNT = 8;
constexpr size_t PUMP_NAME_LENGTH = 32;
constexpr uint16_t CONFIG_VERSION = 2;

struct PumpConfig {
    bool enabled;
    char name[PUMP_NAME_LENGTH];
    float calibrationMlPerSec;
    float doseMl;
    uint8_t hour;
    uint8_t minute;
    uint8_t daysMask;
    float remainingMl;
    uint32_t reservoirSetTimestamp;
    uint32_t lastDoseDate;
    uint32_t lastMissedDoseDate;
};
