#include "AquaCore/Time/RtcWallClock.h"

namespace AquaCore {
namespace Time {
namespace {

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2U;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear =
        (153U * (month > 2U ? month - 3U : month + 9U) + 2U) / 5U +
        day - 1U;
    const unsigned dayOfEra =
        yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return static_cast<int64_t>(era) * 146097LL +
        static_cast<int64_t>(dayOfEra) - 719468LL;
}

bool toUtcTimestamp(const LocalTime& utc, UtcTimestamp& out) {
    if (
        !utc.valid ||
        utc.year < 1970U ||
        !RtcService::isValidUtc(
            utc.year,
            utc.month,
            utc.day,
            utc.hour,
            utc.minute,
            utc.second
        )
    ) {
        return false;
    }

    const int64_t days = daysFromCivil(utc.year, utc.month, utc.day);
    if (days < 0) {
        return false;
    }

    out.secondsSinceUnixEpoch =
        static_cast<uint64_t>(days) * 86400ULL +
        static_cast<uint64_t>(utc.hour) * 3600ULL +
        static_cast<uint64_t>(utc.minute) * 60ULL +
        static_cast<uint64_t>(utc.second);
    return true;
}

} // namespace

RtcWallClock::RtcWallClock(RtcService& rtc) : rtc_(rtc) {}

WallClockReadResult RtcWallClock::readUtc(UtcTimestamp& out) const {
    if (!rtc_.isInitialized()) {
        return WallClockReadResult::Unavailable;
    }

    const LocalTime utc = rtc_.read();
    UtcTimestamp candidate {};
    if (!toUtcTimestamp(utc, candidate)) {
        return WallClockReadResult::Unavailable;
    }

    out = candidate;
    return WallClockReadResult::Success;
}

} // namespace Time
} // namespace AquaCore
