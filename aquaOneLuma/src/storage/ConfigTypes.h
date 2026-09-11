#pragma once

#include <stdint.h>

#include "../profiles/ProfileTypes.h"

namespace LumaSense {

constexpr uint16_t DEVICE_CONFIG_SCHEMA_VERSION = 1U;

enum class IntensityLevel : uint8_t {
    Low = 0,
    Medium,
    High
};

struct ChannelConfig {
    bool enabled = true;

    char name[32] {};

    bool pwmInverted = false;

    float hardMaxPercent = 100.0f;

    float calibrationMinPercent = 0.0f;
    float calibrationMaxPercent = 100.0f;

    float gamma = 1.0f;

    uint16_t ledCount = 0;
    float ledPowerW = 0.0f;

    char ledModel[32] {};
    char spectrumName[32] {};

    float colorTemperatureK = 0.0f;
    float wavelengthNm = 0.0f;

    float opticAngleDeg = 120.0f;
};

struct TankConfig {
    float volumeLiters = 0.0f;

    float tankLengthCm = 0.0f;
    float tankWidthCm = 0.0f;
    float tankHeightCm = 0.0f;

    float waterColumnHeightCm = 0.0f;

    float lampHeightAboveWaterCm = 0.0f;
    float lampLengthCm = 0.0f;

    IntensityLevel intensityLevel = IntensityLevel::Medium;

    bool co2Enabled = false;
};

struct DeviceConfig {
    uint16_t schemaVersion = DEVICE_CONFIG_SCHEMA_VERSION;

    ChannelConfig channels[CHANNEL_COUNT] {};
    Profile profiles[PROFILE_COUNT] {};

    TankConfig tank {};

    uint8_t activeProfileIndex = 0;
    uint8_t serviceProfileIndex = 0;

    float globalPowerLimitPercent = 100.0f;

    char timezone[48] = "Europe/Warsaw";
};

} // namespace LumaSense