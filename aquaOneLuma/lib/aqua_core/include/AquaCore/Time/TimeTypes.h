#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Time {

struct UtcDateTime {
    uint16_t year = 0U;
    uint8_t month = 0U;
    uint8_t day = 0U;
    uint8_t hour = 0U;
    uint8_t minute = 0U;
    uint8_t second = 0U;
};

struct LocalTime {
    bool valid = false;

    uint16_t year = 0U;
    uint8_t month = 0U;
    uint8_t day = 0U;

    uint8_t hour = 0U;
    uint8_t minute = 0U;
    uint8_t second = 0U;

    uint16_t minuteOfDay = 0U;
};

} // namespace Time
} // namespace AquaCore