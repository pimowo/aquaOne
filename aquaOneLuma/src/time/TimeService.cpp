#include "TimeService.h"

namespace LumaSense {

TimeService::TimeService()
    : rtc_(),
      service_(rtc_) {
}

bool TimeService::begin() {
    return service_.begin();
}

LocalTime TimeService::now() {
    return service_.now();
}

bool TimeService::isValid() const {
    return service_.isValid();
}

bool TimeService::rtcInitialized() const {
    return service_.rtcInitialized();
}

const LocalTime& TimeService::current() const {
    return service_.current();
}

AquaCore::Time::RtcService& TimeService::rtcService() {
    return rtc_;
}

const AquaCore::Time::RtcService& TimeService::rtcService() const {
    return rtc_;
}

} // namespace LumaSense