#include "AquaCore/Time/EuropeWarsawTimeService.h"

namespace AquaCore {
namespace Time {
namespace {

bool isLeapYear(uint16_t year) {
    return (
        (year % 4U == 0U && year % 100U != 0U) ||
        (year % 400U == 0U)
    );
}

uint8_t daysInMonth(
    uint16_t year,
    uint8_t month
) {
    static constexpr uint8_t DAYS[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1U || month > 12U) {
        return 0U;
    }

    if (month == 2U && isLeapYear(year)) {
        return 29U;
    }

    return DAYS[month - 1U];
}

uint8_t dayOfWeek(
    uint16_t year,
    uint8_t month,
    uint8_t day
) {
    if (month < 3U) {
        month += 12U;
        --year;
    }

    const uint16_t k = year % 100U;
    const uint16_t j = year / 100U;

    const uint16_t h =
        (
            day +
            (13U * (month + 1U)) / 5U +
            k +
            k / 4U +
            j / 4U +
            5U * j
        ) % 7U;

    return static_cast<uint8_t>((h + 6U) % 7U);
}

uint8_t lastSunday(
    uint16_t year,
    uint8_t month
) {
    const uint8_t lastDay =
        daysInMonth(year, month);

    const uint8_t dow =
        dayOfWeek(year, month, lastDay);

    return static_cast<uint8_t>(lastDay - dow);
}

bool isDstEuropeWarsaw(const LocalTime& utc) {
    if (utc.month < 3U) {
        return false;
    }

    if (utc.month > 10U) {
        return false;
    }

    if (utc.month > 3U && utc.month < 10U) {
        return true;
    }

    if (utc.month == 3U) {
        const uint8_t transitionDay =
            lastSunday(utc.year, 3U);

        if (utc.day > transitionDay) {
            return true;
        }

        if (utc.day < transitionDay) {
            return false;
        }

        return utc.hour >= 1U;
    }

    const uint8_t transitionDay =
        lastSunday(utc.year, 10U);

    if (utc.day < transitionDay) {
        return true;
    }

    if (utc.day > transitionDay) {
        return false;
    }

    return utc.hour < 1U;
}

void addMinutes(
    LocalTime& time,
    uint16_t minutesToAdd
) {
    uint16_t totalMinutes =
        static_cast<uint16_t>(
            time.hour * 60U +
            time.minute +
            minutesToAdd
        );

    while (totalMinutes >= 1440U) {
        totalMinutes -= 1440U;
        ++time.day;

        const uint8_t maximumDay =
            daysInMonth(time.year, time.month);

        if (time.day > maximumDay) {
            time.day = 1U;
            ++time.month;

            if (time.month > 12U) {
                time.month = 1U;
                ++time.year;
            }
        }
    }

    time.hour =
        static_cast<uint8_t>(totalMinutes / 60U);
    time.minute =
        static_cast<uint8_t>(totalMinutes % 60U);
    time.minuteOfDay =
        static_cast<uint16_t>(
            time.hour * 60U + time.minute
        );
}

} // namespace

EuropeWarsawTimeService::EuropeWarsawTimeService(
    RtcService& rtc
) : rtc_(rtc) {
}

bool EuropeWarsawTimeService::begin() {
    current_ = {};

    if (!rtc_.begin()) {
        return false;
    }

    const LocalTime utc = rtc_.read();

    if (!utc.valid) {
        return false;
    }

    current_ = convertUtcToWarsaw(utc);
    return current_.valid;
}

LocalTime EuropeWarsawTimeService::now() {
    const LocalTime utc = rtc_.read();

    if (!utc.valid) {
        current_ = {};
        return current_;
    }

    current_ = convertUtcToWarsaw(utc);
    return current_;
}

bool EuropeWarsawTimeService::isValid() const {
    return current_.valid;
}

bool EuropeWarsawTimeService::rtcInitialized() const {
    return rtc_.isInitialized();
}

const LocalTime&
EuropeWarsawTimeService::current() const {
    return current_;
}

LocalTime EuropeWarsawTimeService::convertUtcToWarsaw(
    const LocalTime& utc
) {
    if (!utc.valid) {
        return {};
    }

    LocalTime local = utc;
    const bool dst = isDstEuropeWarsaw(utc);
    const uint16_t offsetMinutes =
        dst ? 120U : 60U;

    addMinutes(local, offsetMinutes);
    local.valid = true;
    return local;
}

} // namespace Time
} // namespace AquaCore