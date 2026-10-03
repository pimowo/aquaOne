#pragma once

#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/System/RuntimeStateCoordinator.h"

namespace AquaCore {
namespace Network {

// Borrows the live NetworkService. Network never contributes Safety.
class NetworkHealthProvider final : public System::HealthProvider {
public:
    explicit NetworkHealthProvider(const NetworkService& service)
        : service_(service) {}

    System::HealthState healthContribution() const override;

private:
    const NetworkService& service_;
};

} // namespace Network
} // namespace AquaCore
