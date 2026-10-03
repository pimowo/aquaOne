#pragma once

#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/System/Startup.h"

namespace AquaCore {
namespace Network {

// Borrowed by ApplicationRuntime with its config and service. Composition
// places this optional participant in NETWORK_INIT after local startup.
class NetworkStartup {
public:
    NetworkStartup(NetworkService& service, const NetworkConfig& config)
        : service_(service), config_(config) {}

    NetworkStartup(const NetworkStartup&) = delete;
    NetworkStartup& operator=(const NetworkStartup&) = delete;

    System::StartupParticipant participant();
    static System::StartupStepResult run(void* context);

private:
    NetworkService& service_;
    const NetworkConfig& config_;
};

} // namespace Network
} // namespace AquaCore
