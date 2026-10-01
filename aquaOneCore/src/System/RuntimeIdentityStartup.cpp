#include "AquaCore/System/RuntimeIdentityStartup.h"

namespace AquaCore {
namespace Identity {
namespace {

System::StartupStepResult generationFailed() {
    return System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic(
            "AQUA.CORE", "RUNTIME_IDENTITY_GENERATION_FAILED"
        )
    );
}

} // namespace

System::StartupParticipant RuntimeIdentityStartup::participant() {
    System::StartupParticipant value {};
    value.action.participantId = "runtime_identity";
    value.action.callback = &RuntimeIdentityStartup::run;
    value.action.context = this;
    value.phase = System::StartupPhase::CORE_INIT;
    value.requirement = System::StartupRequirement::REQUIRED;
    return value;
}

System::StartupStepResult RuntimeIdentityStartup::run(void* context) {
    if (context == nullptr) {
        return generationFailed();
    }
    RuntimeIdentityStartup* self = static_cast<RuntimeIdentityStartup*>(context);
    return self->state_.initialize(self->generator_)
        ? System::StartupStepResult::succeeded()
        : generationFailed();
}

} // namespace Identity
} // namespace AquaCore
