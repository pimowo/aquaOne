#include "LightEngine.h"

#include <algorithm>
#include <cmath>

#include "../../include/Constants.h"

namespace LumaSense {
namespace {

bool isFinitePercent(float value) {
    return std::isfinite(value) &&
           value >= LEVEL_MIN_PERCENT &&
           value <= LEVEL_MAX_PERCENT;
}

} // namespace

ChannelLevels LightEngine::process(
    const ChannelLevels& requested,
    const DeviceConfig& config
) const {
    ChannelLevels result {};

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        result.value[channel] = processChannel(
            requested.value[channel],
            config.channels[channel],
            config.globalPowerLimitPercent
        );
    }

    return result;
}

float LightEngine::processChannel(
    float requestedPercent,
    const ChannelConfig& channelConfig,
    float globalPowerLimitPercent
) const {
    if (!channelConfig.enabled) {
        return 0.0f;
    }

    // Last-line protection: malformed runtime data must never reach PWM.
    if (
        !std::isfinite(requestedPercent) ||
        !isFinitePercent(globalPowerLimitPercent) ||
        !isFinitePercent(channelConfig.hardMaxPercent) ||
        !isFinitePercent(channelConfig.calibrationMinPercent) ||
        !isFinitePercent(channelConfig.calibrationMaxPercent) ||
        channelConfig.calibrationMinPercent >
            channelConfig.calibrationMaxPercent ||
        !std::isfinite(channelConfig.gamma) ||
        channelConfig.gamma <= 0.0f
    ) {
        return 0.0f;
    }

    float value = clampPercent(requestedPercent);

    // Zero is an explicit physical OFF and must bypass calibrationMin.
    if (value <= 0.0f) {
        return 0.0f;
    }

    value *= globalPowerLimitPercent / 100.0f;

    // A zero global limit must also remain a physical OFF.
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }

    const float x = value / 100.0f;
    const float xGamma = std::pow(x, channelConfig.gamma);

    if (!std::isfinite(xGamma)) {
        return 0.0f;
    }

    const float calibrated =
        channelConfig.calibrationMinPercent +
        xGamma * (
            channelConfig.calibrationMaxPercent -
            channelConfig.calibrationMinPercent
        );

    if (!std::isfinite(calibrated)) {
        return 0.0f;
    }

    const float limited =
        std::min(calibrated, channelConfig.hardMaxPercent);

    return clampPercent(limited);
}

float LightEngine::clampPercent(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }

    return std::clamp(
        value,
        LEVEL_MIN_PERCENT,
        LEVEL_MAX_PERCENT
    );
}

} // namespace LumaSense
