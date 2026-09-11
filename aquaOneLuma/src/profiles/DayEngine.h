#pragma once

#include <stdint.h>

#include "ProfileTypes.h"
#include "../core/RuntimeTypes.h"

namespace LumaSense {

struct DayCalculation {
    ChannelLevels levels {};

    DayState dayState =
        DayState::Day;

    uint8_t currentStageIndex = 0;
    uint8_t nextStageIndex = 0;

    float segmentProgress = 0.0f;
};

class DayEngine {
public:

    // -----------------------------------------------------
    // Stara wersja minutowa.
    //
    // Zostawiamy ją na razie dla kompatybilności.
    // -----------------------------------------------------

    DayCalculation calculate(
        const Profile& profile,
        uint16_t minuteOfDay
    ) const;

    // -----------------------------------------------------
    // Nowa wersja dokładna.
    //
    // secondOfDay:
    // 0 .. 86399
    // -----------------------------------------------------

    DayCalculation calculateSeconds(
        const Profile& profile,
        uint32_t secondOfDay
    ) const;

private:

    float smoothstep(
        float value
    ) const;
};

} // namespace LumaSense