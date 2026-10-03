#pragma once

#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Time/NtpService.h"

namespace AquaCore {
namespace Time {

// Borrowed runtime infrastructure. Call after NetworkService::update(); it
// never changes startup, Health, Safety, or the consumer-facing WallClock.
class NetworkNtpSync {
public:
    NetworkNtpSync(
        NtpService& ntp,
        const Network::NetworkService& network
    ) : ntp_(ntp), network_(network) {}

    void update();

private:
    NtpService& ntp_;
    const Network::NetworkService& network_;
};

} // namespace Time
} // namespace AquaCore
