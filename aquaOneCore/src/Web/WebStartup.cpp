#include "AquaCore/Web/WebStartup.h"

namespace AquaCore {
namespace Web {
namespace {

System::StartupStepResult initializationFailed() {
    return System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic(
            "AQUA.CORE", "WEB_INIT_FAILED"
        )
    );
}

} // namespace

System::StartupParticipant WebStartup::participant() {
    System::StartupParticipant value {};
    value.action.participantId = "web";
    value.action.callback = &WebStartup::run;
    value.action.context = this;
    value.phase = System::StartupPhase::INTERFACES_INIT;
    value.requirement = System::StartupRequirement::OPTIONAL;
    return value;
}

System::StartupStepResult WebStartup::run(void* context) {
    if (context == nullptr) {
        return initializationFailed();
    }
    WebStartup& self = *static_cast<WebStartup*>(context);
    if (!self.service_.begin(self.config_)) {
        return initializationFailed();
    }
    return self.config_.enabled
        ? System::StartupStepResult::succeeded()
        : System::StartupStepResult::disabled();
}

} // namespace Web
} // namespace AquaCore
