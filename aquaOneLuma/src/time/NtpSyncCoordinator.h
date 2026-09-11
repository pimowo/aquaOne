#pragma once

#include <stdint.h>

#include "NtpService.h"

namespace LumaSense {

// Production scheduling policy around the reusable NtpService.
// NTP remains an optional UTC correction of DS3231; it is never the
// clock source consumed directly by FirmwareApp or LumaCore.
class NtpSyncCoordinator {
public:
    explicit NtpSyncCoordinator(NtpService& service);

    bool begin(uint32_t nowMs = 0U);
    void update(bool wifiAvailable, uint32_t nowMs);

private:
    NtpService& service_;
    bool wifiWasAvailable_ = false;
};

} // namespace LumaSense