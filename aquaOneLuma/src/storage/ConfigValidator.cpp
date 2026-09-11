#include "ConfigValidator.h"

#include <cmath>
#include <cstddef>

#include "../../include/Constants.h"

namespace LumaSense {
namespace {

bool isFiniteInRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool isFiniteNonNegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool isNullTerminated(const char* value, size_t capacity) {
    if (value == nullptr || capacity == 0) {
        return false;
    }

    for (size_t index = 0; index < capacity; ++index) {
        if (value[index] == '\0') {
            return true;
        }
    }

    return false;
}

bool validateLevels(const ChannelLevels& levels) {
    for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        if (!isFiniteInRange(
                levels.value[channel],
                LEVEL_MIN_PERCENT,
                LEVEL_MAX_PERCENT
            )) {
            return false;
        }
    }

    return true;
}

} // namespace

bool ConfigValidator::validate(const DeviceConfig& config) {
    if (
        config.activeProfileIndex >= PROFILE_COUNT ||
        config.serviceProfileIndex >= PROFILE_COUNT ||
        !isFiniteInRange(
            config.globalPowerLimitPercent,
            LEVEL_MIN_PERCENT,
            LEVEL_MAX_PERCENT
        ) ||
        !isNullTerminated(config.timezone, sizeof(config.timezone)) ||
        !validateTank(config.tank)
    ) {
        return false;
    }

    for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        if (!validateChannel(config.channels[channel])) {
            return false;
        }
    }

    for (size_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        if (!validateProfile(config.profiles[profile])) {
            return false;
        }
    }

    return true;
}

bool ConfigValidator::validateChannel(const ChannelConfig& channel) {
    return
        isNullTerminated(channel.name, sizeof(channel.name)) &&
        isNullTerminated(channel.ledModel, sizeof(channel.ledModel)) &&
        isNullTerminated(
            channel.spectrumName,
            sizeof(channel.spectrumName)
        ) &&
        isFiniteInRange(
            channel.hardMaxPercent,
            LEVEL_MIN_PERCENT,
            LEVEL_MAX_PERCENT
        ) &&
        isFiniteInRange(
            channel.calibrationMinPercent,
            LEVEL_MIN_PERCENT,
            LEVEL_MAX_PERCENT
        ) &&
        isFiniteInRange(
            channel.calibrationMaxPercent,
            LEVEL_MIN_PERCENT,
            LEVEL_MAX_PERCENT
        ) &&
        channel.calibrationMinPercent <=
            channel.calibrationMaxPercent &&
        std::isfinite(channel.gamma) &&
        channel.gamma > 0.0f &&
        isFiniteNonNegative(channel.ledPowerW) &&
        isFiniteNonNegative(channel.colorTemperatureK) &&
        isFiniteNonNegative(channel.wavelengthNm) &&
        isFiniteInRange(channel.opticAngleDeg, 0.0f, 180.0f);
}

bool ConfigValidator::validateProfile(const Profile& profile) {
    if (
        !isNullTerminated(profile.name, sizeof(profile.name)) ||
        profile.dayStartMinute >= MINUTES_PER_DAY ||
        profile.dayEndMinute >= MINUTES_PER_DAY ||
        profile.dayStartMinute >= profile.dayEndMinute
    ) {
        return false;
    }

    for (size_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        if (!validateLevels(profile.stages[stage].levels)) {
            return false;
        }
    }

    return validateLevels(profile.nightLevels);
}

bool ConfigValidator::validateTank(const TankConfig& tank) {
    return
        isFiniteNonNegative(tank.volumeLiters) &&
        isFiniteNonNegative(tank.tankLengthCm) &&
        isFiniteNonNegative(tank.tankWidthCm) &&
        isFiniteNonNegative(tank.tankHeightCm) &&
        isFiniteNonNegative(tank.waterColumnHeightCm) &&
        isFiniteNonNegative(tank.lampHeightAboveWaterCm) &&
        isFiniteNonNegative(tank.lampLengthCm) &&
        static_cast<uint8_t>(tank.intensityLevel) <=
            static_cast<uint8_t>(IntensityLevel::High);
}

} // namespace LumaSense
