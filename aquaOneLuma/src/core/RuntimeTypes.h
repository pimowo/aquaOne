#pragma once

#include <stdint.h>

#include "../profiles/ProfileTypes.h"

namespace LumaSense {

enum class OperatingMode : uint8_t {
    Normal = 0,
    Service,
    Manual,
    ChannelTest,
    Preview,
    Simulation,
    Off
};

enum class DayState : uint8_t {
    Day = 0,
    Night
};

struct RuntimeState {
    OperatingMode mode = OperatingMode::Normal;
    OperatingMode returnMode = OperatingMode::Normal;

    DayState dayState = DayState::Day;

    bool timeValid = false;

    uint8_t currentStageIndex = 0;
    uint8_t nextStageIndex = 0;

    uint16_t currentMinuteOfDay = 0;

    ChannelLevels requestedLevels {};
    ChannelLevels actualLevels {};

    bool transitionActive = false;
};

} // namespace LumaSense