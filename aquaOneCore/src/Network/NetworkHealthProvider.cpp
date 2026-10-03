#include "AquaCore/Network/NetworkHealthProvider.h"

namespace AquaCore {
namespace Network {

System::HealthState NetworkHealthProvider::healthContribution() const {
    switch (service_.state()) {
        case NetworkState::Disabled:
        case NetworkState::Idle:
        case NetworkState::Connecting:
        case NetworkState::Connected:
            return System::HealthState::OK;
        case NetworkState::Disconnected:
        case NetworkState::Error:
            return System::HealthState::DEGRADED;
    }
    return System::HealthState::DEGRADED;
}

} // namespace Network
} // namespace AquaCore
