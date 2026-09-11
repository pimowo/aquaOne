#include "AquaCore/Time/ResilientTimeService.h"

namespace AquaCore {
namespace Time {

namespace {

constexpr int daysFromCivil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

struct CivilDate { int year; unsigned month; unsigned day; };

constexpr CivilDate civilFromDays(int z) noexcept {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return CivilDate{y + (m <= 2), m, d};
}

LocalTime addSeconds(const LocalTime& base, uint32_t secondsToAdd) {
    if (!base.valid) {
        return {};
    }

    const int days = daysFromCivil(base.year, base.month, base.day);
    const uint32_t totalSec = static_cast<uint32_t>(base.hour) * 3600U +
                              static_cast<uint32_t>(base.minute) * 60U +
                              static_cast<uint32_t>(base.second) +
                              secondsToAdd;

    const uint32_t extraDays = totalSec / 86400U;
    const uint32_t daySec = totalSec % 86400U;

    const CivilDate cd = civilFromDays(days + static_cast<int>(extraDays));

    LocalTime result {};
    result.valid = true;
    result.year = static_cast<uint16_t>(cd.year);
    result.month = static_cast<uint8_t>(cd.month);
    result.day = static_cast<uint8_t>(cd.day);
    result.hour = static_cast<uint8_t>(daySec / 3600U);
    result.minute = static_cast<uint8_t>((daySec % 3600U) / 60U);
    result.second = static_cast<uint8_t>(daySec % 60U);
    result.minuteOfDay = static_cast<uint16_t>(result.hour * 60U + result.minute);
    return result;
}

} // namespace

ResilientTimeService::ResilientTimeService(
    RtcService& rtc,
    const ResilientTimeConfig& config
) : rtc_(rtc),
    config_(config) {
    if (config_.failureThreshold == 0U) {
        config_.failureThreshold = 1U;
    }
    if (config_.recoveryThreshold == 0U) {
        config_.recoveryThreshold = 1U;
    }
}

bool ResilientTimeService::begin(uint32_t nowMs) {
    if (!rtc_.begin()) {
        rtcHealthy_ = false;
        hasValidCache_ = false;
        consecutiveFailures_ = 1U;
        consecutiveSuccesses_ = 0U;
        return false;
    }

    const LocalTime reading = rtc_.read();
    if (!reading.valid) {
        rtcHealthy_ = false;
        hasValidCache_ = false;
        consecutiveFailures_ = 1U;
        consecutiveSuccesses_ = 0U;
        return false;
    }

    cachedUtc_ = reading;
    cachedAtMs_ = nowMs;
    hasValidCache_ = true;
    rtcHealthy_ = true;
    consecutiveFailures_ = 0U;
    consecutiveSuccesses_ = config_.recoveryThreshold;
    return true;
}

void ResilientTimeService::poll(uint32_t nowMs) {
    const LocalTime reading = rtc_.read();

    if (reading.valid) {
        cachedUtc_ = reading;
        cachedAtMs_ = nowMs;
        hasValidCache_ = true;
        consecutiveFailures_ = 0U;

        if (!rtcHealthy_) {
            if (consecutiveSuccesses_ < UINT8_MAX) {
                ++consecutiveSuccesses_;
            }
            if (consecutiveSuccesses_ >= config_.recoveryThreshold) {
                rtcHealthy_ = true;
            }
        } else {
            consecutiveSuccesses_ = config_.recoveryThreshold;
        }
    } else {
        consecutiveSuccesses_ = 0U;
        if (consecutiveFailures_ < UINT8_MAX) {
            ++consecutiveFailures_;
        }
        if (consecutiveFailures_ >= config_.failureThreshold) {
            rtcHealthy_ = false;
        }
    }
}

LocalTime ResilientTimeService::now(uint32_t nowMs) const {
    if (!hasValidCache_) {
        return {};
    }

    const uint32_t elapsedMs = nowMs - cachedAtMs_;
    const uint32_t elapsedSec = elapsedMs / 1000U;

    return addSeconds(cachedUtc_, elapsedSec);
}

bool ResilientTimeService::isValid() const {
    return hasValidCache_;
}

bool ResilientTimeService::isRtcHealthy() const {
    return rtcHealthy_;
}

uint8_t ResilientTimeService::consecutiveFailures() const {
    return consecutiveFailures_;
}

uint8_t ResilientTimeService::consecutiveSuccesses() const {
    return consecutiveSuccesses_;
}

const LocalTime& ResilientTimeService::cachedUtc() const {
    return cachedUtc_;
}

uint32_t ResilientTimeService::cachedAtMs() const {
    return cachedAtMs_;
}

void ResilientTimeService::syncUtc(const LocalTime& utc, uint32_t nowMs) {
    if (!utc.valid) {
        return;
    }

    if (!RtcService::isValidUtc(
        utc.year,
        utc.month,
        utc.day,
        utc.hour,
        utc.minute,
        utc.second
    )) {
        return;
    }

    cachedUtc_ = utc;
    cachedAtMs_ = nowMs;
    hasValidCache_ = true;
}

} // namespace Time
} // namespace AquaCore
