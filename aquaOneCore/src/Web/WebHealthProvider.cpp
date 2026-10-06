#include "AquaCore/Web/WebHealthProvider.h"

namespace AquaCore {
namespace Web {

System::HealthState WebHealthProvider::healthContribution() const {
    if (service_.state() == NativeWebState::Disabled) {
        return System::HealthState::OK;
    }
    return service_.isRunning()
        ? System::HealthState::OK
        : System::HealthState::DEGRADED;
}

} // namespace Web
} // namespace AquaCore
