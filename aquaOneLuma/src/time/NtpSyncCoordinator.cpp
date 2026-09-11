#include "NtpSyncCoordinator.h"

namespace LumaSense {

NtpSyncCoordinator::NtpSyncCoordinator(NtpService& service)
    : service_(service) {
}

bool NtpSyncCoordinator::begin(uint32_t nowMs) {
    wifiWasAvailable_ = false;
    return service_.begin(nowMs);
}

void NtpSyncCoordinator::update(
    bool wifiAvailable,
    uint32_t nowMs
) {
    // A new Wi-Fi session gets one immediate, non-blocking attempt.
    // A failed attempt is not retried in a tight loop; the NtpService
    // periodic anchor or a later Wi-Fi reconnect schedules the next one.
    if (wifiAvailable && !wifiWasAvailable_) {
        (void)service_.requestSync(true, nowMs);
    }

    service_.update(wifiAvailable, nowMs);

    if (wifiAvailable) {
        (void)service_.requestPeriodicSync(true, nowMs);
    }

    wifiWasAvailable_ = wifiAvailable;
}

} // namespace LumaSense