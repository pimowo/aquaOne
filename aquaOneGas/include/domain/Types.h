#pragma once
#include <stdint.h>

namespace gassense {

enum class BottleState : uint8_t {
    Ok,
    Low,
    Critical,
    Empty,
    Missing
};

enum class DeviceStatus : uint8_t {
    Ok,
    Warning,
    Critical,
    Fault
};

enum class MeasurementStability : uint8_t {
    Unknown,
    Stable,
    Unstable
};

enum class SensorHealth : uint8_t {
    Disabled,
    Initializing,
    Ok,
    Fault,
    Recovering
};

struct Measurements {
    float totalMassKg = 0.0f;
    float filteredMassKg = 0.0f;
    float pressureBar = 0.0f;
    float bottleTempC = 0.0f;

    long hx711Raw = 0;
    int16_t ads1115Raw = 0;

    MeasurementStability weightStability = MeasurementStability::Unknown;
    SensorHealth hx711 = SensorHealth::Initializing;
    SensorHealth ads1115 = SensorHealth::Disabled;
    SensorHealth ds18b20 = SensorHealth::Disabled;
};

struct GasState {
    float dryMassKg = 0.0f;
    float co2RemainingKg = 0.0f;
    float co2RemainingPercent = 0.0f;
    float co2UsedKg = 0.0f;

    BottleState bottle = BottleState::Missing;
    DeviceStatus status = DeviceStatus::Fault;

    bool co2Low = false;
    bool co2Critical = false;
    bool bottleMissing = false;
    bool rapidMassDrop = false;
    bool pressureFault = false;
    bool sensorFault = false;
};

} // namespace gassense
