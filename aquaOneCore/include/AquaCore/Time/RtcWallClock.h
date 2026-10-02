#pragma once

#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/WallClock.h"

namespace AquaCore {
namespace Time {

// Read-only projection of a borrowed RTC source into the UTC WallClock
// capability. The RtcService owner must outlive this adapter.
class RtcWallClock final : public WallClock {
public:
    explicit RtcWallClock(RtcService& rtc);
    RtcWallClock(RtcService&&) = delete;
    RtcWallClock(const RtcService&&) = delete;

    WallClockReadResult readUtc(UtcTimestamp& out) const override;

private:
    RtcService& rtc_;
};

} // namespace Time
} // namespace AquaCore
