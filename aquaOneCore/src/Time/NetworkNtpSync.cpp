#include "AquaCore/Time/NetworkNtpSync.h"

namespace AquaCore {
namespace Time {

void NetworkNtpSync::update() {
    const bool connected =
        network_.state() == Network::NetworkState::Connected;
    ntp_.update(connected);

    if (!connected || !ntp_.isInitialized()) {
        return;
    }

    if (!ntp_.hasSyncResult() && !ntp_.isSyncInProgress()) {
        (void)ntp_.requestSync(true);
    } else {
        (void)ntp_.requestPeriodicSync(true);
    }
}

} // namespace Time
} // namespace AquaCore
