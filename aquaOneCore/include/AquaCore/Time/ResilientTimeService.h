#pragma once

#include <stdint.h>

#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/TimeTypes.h"

namespace AquaCore {
namespace Time {

struct ResilientTimeConfig {
    bool rtcEnabled = true;
    uint8_t failureThreshold = 3U;
    uint8_t recoveryThreshold = 3U;
    uint32_t unhealthyProbeIntervalMs = 30000U;
};

class ResilientTimeService {
public:
    explicit ResilientTimeService(
        RtcService& rtc,
        const ResilientTimeConfig& config = ResilientTimeConfig {}
    );

    bool begin(uint32_t nowMs = 0U);
    void poll(uint32_t nowMs);

    LocalTime now(uint32_t nowMs) const;

    bool isValid() const;
    bool isRtcConfigured() const;
    bool isRtcHealthy() const;
    bool isProbeDue(uint32_t nowMs) const;

    uint8_t consecutiveFailures() const;
    uint8_t consecutiveSuccesses() const;
    const LocalTime& cachedUtc() const;
    uint32_t cachedAtMs() const;

    void syncUtc(const LocalTime& utc, uint32_t nowMs);

private:
    RtcService& rtc_;
    ResilientTimeConfig config_;
    LocalTime cachedUtc_ {};
    uint32_t cachedAtMs_ = 0U;
    uint32_t lastPollMs_ = 0U;
    uint8_t consecutiveFailures_ = 0U;
    uint8_t consecutiveSuccesses_ = 0U;
    bool rtcHealthy_ = false;
    bool hasValidCache_ = false;
};

} // namespace Time
} // namespace AquaCore
