#pragma once
#include <stdint.h>

namespace gassense {

struct GasSenseConfig {
    uint32_t schemaVersion = 1;

    // Bottle
    float bottleTareKg = 0.0f;
    float equipmentMassKg = 0.0f;
    float nominalCo2Kg = 5.0f;
    float newBottleToleranceKg = 0.20f;

    // Alarms
    float lowPercent = 20.0f;
    float criticalPercent = 10.0f;
    float emptyKg = 0.10f;

    // Missing bottle
    float missingDryMassFactor = 0.50f;
    uint32_t missingConfirmMs = 5000;

    // Rapid drop
    float rapidDropKg = 0.50f;
    uint32_t rapidDropConfirmMs = 3000;

    // Optional sensors
    bool pressureEnabled = false;
    bool bottleTemperatureEnabled = false;

    // Pressure
    float pressureMinBar = 0.5f;
    float pressureMaxBar = 4.0f;
    uint32_t pressureAlarmConfirmMs = 5000;
    float pressureCalOffset = 0.0f;
    float pressureCalScale = 1.0f;

    // Weight calibration
    float hx711Scale = 1.0f;
    long hx711Offset = 0;

    // Audio
    bool soundEnabled = true;

    // Identity
    char deviceName[33] = "GasSense CO2";
};

} // namespace gassense
