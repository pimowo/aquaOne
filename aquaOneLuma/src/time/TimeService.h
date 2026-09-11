#pragma once

#include "AquaCore/Time/EuropeWarsawTimeService.h"
#include "RtcService.h"
#include "TimeTypes.h"

namespace LumaSense {

class TimeService {
public:
    TimeService();

    bool begin();
    LocalTime now();

    bool isValid() const;
    bool rtcInitialized() const;
    const LocalTime& current() const;
    AquaCore::Time::RtcService& rtcService();
    const AquaCore::Time::RtcService& rtcService() const;

private:
    RtcService rtc_;
    AquaCore::Time::EuropeWarsawTimeService service_;
};

} // namespace LumaSense
