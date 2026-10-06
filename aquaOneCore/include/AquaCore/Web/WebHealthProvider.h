#pragma once

#include "AquaCore/System/RuntimeStateCoordinator.h"
#include "AquaCore/Web/NativeWebService.h"

namespace AquaCore {
namespace Web {

// Borrows live Web state. Web contributes Health only, never Safety.
class WebHealthProvider final : public System::HealthProvider {
public:
    explicit WebHealthProvider(const NativeWebService& service)
        : service_(service) {}

    System::HealthState healthContribution() const override;

private:
    const NativeWebService& service_;
};

} // namespace Web
} // namespace AquaCore
