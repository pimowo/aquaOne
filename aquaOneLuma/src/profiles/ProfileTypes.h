#pragma once

#include <stdint.h>

#include "../../include/Constants.h"

namespace LumaSense {

struct ChannelLevels {
    float value[CHANNEL_COUNT] {};
};

enum class DayStageId : uint8_t {
    SunriseStart = 0,
    Morning,
    Forenoon,
    Noon,
    Afternoon,
    Evening,
    Twilight,
    Sunset
};

struct DayStage {
    DayStageId id {};
    ChannelLevels levels {};
};

struct Profile {
    char name[32] {};

    uint16_t dayStartMinute = 480;
    uint16_t dayEndMinute = 1140;

    DayStage stages[DAY_STAGE_COUNT] {};

    bool nightEnabled = false;
    ChannelLevels nightLevels {};

    bool generated = false;
    uint16_t generatorVersion = 0;
    bool generatorOutdated = false;
};

} // namespace LumaSense