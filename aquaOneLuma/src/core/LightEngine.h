#pragma once

#include "../profiles/ProfileTypes.h"
#include "../storage/ConfigTypes.h"

namespace LumaSense {

class LightEngine {
public:
    ChannelLevels process(
        const ChannelLevels& requested,
        const DeviceConfig& config
    ) const;

private:
    float processChannel(
        float requestedPercent,
        const ChannelConfig& channelConfig,
        float globalPowerLimitPercent
    ) const;

    static float clampPercent(float value);
};

} // namespace LumaSense