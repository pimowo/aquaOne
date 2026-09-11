#pragma once

#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/TimeTypes.h"

namespace AquaCore {
namespace Time {

class EuropeWarsawTimeService {
public:
    explicit EuropeWarsawTimeService(RtcService& rtc);

    bool begin();
    LocalTime now();

    bool isValid() const;
    bool rtcInitialized() const;
    const LocalTime& current() const;

    static LocalTime convertUtcToWarsaw(
        const LocalTime& utc
    );

private:
    RtcService& rtc_;
    LocalTime current_ {};
};

} // namespace Time
} // namespace AquaCore