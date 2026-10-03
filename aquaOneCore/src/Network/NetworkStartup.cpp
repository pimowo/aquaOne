#include "AquaCore/Network/NetworkStartup.h"

namespace AquaCore {
namespace Network {
namespace {

System::StartupStepResult initializationFailed() {
    return System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic(
            "AQUA.CORE", "NETWORK_INIT_FAILED"
        )
    );
}

} // namespace

System::StartupParticipant NetworkStartup::participant() {
    System::StartupParticipant value {};
    value.action.participantId = "network";
    value.action.callback = &NetworkStartup::run;
    value.action.context = this;
    value.phase = System::StartupPhase::NETWORK_INIT;
    value.requirement = System::StartupRequirement::OPTIONAL;
    return value;
}

System::StartupStepResult NetworkStartup::run(void* context) {
    if (context == nullptr) {
        return initializationFailed();
    }
    NetworkStartup& self = *static_cast<NetworkStartup*>(context);
    if (!self.service_.begin(self.config_)) {
        return initializationFailed();
    }
    return !self.config_.staEnabled && !self.config_.apEnabled
        ? System::StartupStepResult::disabled()
        : System::StartupStepResult::succeeded();
}

} // namespace Network
} // namespace AquaCore
