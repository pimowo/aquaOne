#include "HydroSenseConfigValidator.h"

#include <cmath>
#include <stddef.h>

namespace
{
bool isNullTerminated(const char* text, size_t capacity)
{
    if (text == nullptr || capacity == 0U) return false;
    for (size_t index = 0U; index < capacity; ++index)
        if (text[index] == '\0') return true;
    return false;
}
}

bool validateHydroSenseConfig(const HydroSenseConfig& config)
{
    if (!std::isfinite(config.ultrasonicMinDistanceCm) ||
        !std::isfinite(config.ultrasonicMaxDistanceCm) ||
        !std::isfinite(config.tankEmptyDistanceCm) ||
        !std::isfinite(config.tankFullDistanceCm) ||
        !std::isfinite(config.reserveLowPercent) ||
        !std::isfinite(config.reserveCriticalPercent) ||
        !std::isfinite(config.reserveHysteresisPercent)) return false;
    if (config.floatDebounceMs > 5000U) return false;
    if (config.ultrasonicMinDistanceCm < 0.5f ||
        config.ultrasonicMaxDistanceCm <= config.ultrasonicMinDistanceCm ||
        config.ultrasonicMaxDistanceCm > 1000.0f ||
        config.ultrasonicTimeoutUs < 1000U ||
        config.ultrasonicTimeoutUs > 100000U) return false;
    if (config.tankFullDistanceCm < 0.0f ||
        config.tankEmptyDistanceCm <= config.tankFullDistanceCm ||
        config.tankSampleIntervalMs < 20U ||
        config.tankSampleIntervalMs > 10000U ||
        config.tankMaxFailedSeries == 0U ||
        config.tankMaxFailedSeries > 100U) return false;
    if (config.reserveCriticalPercent < 0.0f ||
        config.reserveCriticalPercent > 100.0f ||
        config.reserveLowPercent < 0.0f ||
        config.reserveLowPercent > 100.0f ||
        config.reserveCriticalPercent >= config.reserveLowPercent ||
        config.reserveHysteresisPercent < 0.0f ||
        config.reserveHysteresisPercent > 20.0f) return false;
    if (config.topupStartDelayMs > 600000U ||
        config.topupMaxPumpRuntimeMs < 1000U ||
        config.topupMaxPumpRuntimeMs > 3600000U) return false;
    if (!isNullTerminated(config.wifiSsid, sizeof(config.wifiSsid)) ||
        !isNullTerminated(config.wifiPassword, sizeof(config.wifiPassword)) ||
        !isNullTerminated(config.wifiHostname, sizeof(config.wifiHostname)) ||
        !isNullTerminated(config.wifiApSsid, sizeof(config.wifiApSsid)) ||
        !isNullTerminated(config.wifiApPassword, sizeof(config.wifiApPassword)))
        return false;
    if (config.wifiStaEnabled && config.wifiSsid[0] == '\0') return false;
    if (config.wifiAutoReconnect && config.wifiStaEnabled &&
        config.wifiReconnectIntervalMs == 0U) return false;
    if (config.wifiApEnabled && config.wifiApSsid[0] == '\0') return false;
    return true;
}
