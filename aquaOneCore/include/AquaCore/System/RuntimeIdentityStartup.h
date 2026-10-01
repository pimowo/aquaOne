#pragma once

#include "AquaCore/System/RuntimeIdentity.h"
#include "AquaCore/System/Startup.h"

namespace AquaCore {
namespace Identity {

// Borrowed by ApplicationRuntime. Composition Root places this participant
// first among REQUIRED CORE_INIT participants for each runtime instance.
class RuntimeIdentityStartup {
public:
    RuntimeIdentityStartup(
        RuntimeIdentityState& state,
        RuntimeIdentityGenerator& generator
    ) : state_(state), generator_(generator) {}

    RuntimeIdentityStartup(const RuntimeIdentityStartup&) = delete;
    RuntimeIdentityStartup& operator=(const RuntimeIdentityStartup&) = delete;

    System::StartupParticipant participant();
    static System::StartupStepResult run(void* context);

private:
    RuntimeIdentityState& state_;
    RuntimeIdentityGenerator& generator_;
};

} // namespace Identity
} // namespace AquaCore
