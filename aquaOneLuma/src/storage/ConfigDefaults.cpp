#include "ConfigDefaults.h"

#include "../../include/Constants.h"

namespace LumaSense {

DeviceConfig createDefaultConfig() {
    DeviceConfig config {};

    config.schemaVersion =
        DEVICE_CONFIG_SCHEMA_VERSION;

    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 0;
    config.globalPowerLimitPercent = 100.0f;

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        ChannelConfig& channelConfig =
            config.channels[channel];

        channelConfig.enabled = true;
        channelConfig.pwmInverted = false;
        channelConfig.hardMaxPercent = 100.0f;
        channelConfig.calibrationMinPercent = 0.0f;
        channelConfig.calibrationMaxPercent = 100.0f;
        channelConfig.gamma = 1.0f;
        channelConfig.opticAngleDeg = 120.0f;
    }

    for (
        uint8_t profile = 0;
        profile < PROFILE_COUNT;
        ++profile
    ) {
        config.profiles[profile].dayStartMinute = 480;
        config.profiles[profile].dayEndMinute = 1140;

        for (
            uint8_t stage = 0;
            stage < DAY_STAGE_COUNT;
            ++stage
        ) {
            config.profiles[profile].stages[stage].id =
                static_cast<DayStageId>(stage);
        }
    }

    return config;
}

} // namespace LumaSense