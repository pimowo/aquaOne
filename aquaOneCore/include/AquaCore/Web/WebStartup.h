#pragma once

#include "AquaCore/System/Startup.h"
#include "AquaCore/Web/NativeWebService.h"

namespace AquaCore {
namespace Web {

// Borrows service and config. Composition places this optional participant in
// INTERFACES_INIT after network prerequisites.
class WebStartup {
public:
    WebStartup(NativeWebService& service, const WebConfig& config)
        : service_(service), config_(config) {}
    WebStartup(NativeWebService&, WebConfig&&) = delete;

    System::StartupParticipant participant();
    static System::StartupStepResult run(void* context);

private:
    NativeWebService& service_;
    const WebConfig& config_;
};

} // namespace Web
} // namespace AquaCore
