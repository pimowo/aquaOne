#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Maintenance {

enum class MaintenanceParticipantResult : uint8_t {
    Prepared,
    Rejected,
    Failed
};

// One synchronous, borrowed project/Domain preparation capability. The
// Composition Root owns the concrete object, which must outlive its future
// orchestrator. Core never deletes through this interface.
class MaintenanceParticipant {
public:
    // Prepared: required work is complete and the target mode may be committed
    // by the lifecycle owner. This method does not write OperationalState.
    // Rejected: no unresolved transition effects; the source mode remains valid.
    // Failed: neither source nor target mode can be guaranteed; future
    // orchestration must take a fail-safe path instead of treating it as refusal.
    virtual MaintenanceParticipantResult prepareEnter() = 0;
    virtual MaintenanceParticipantResult prepareExit() = 0;

protected:
    // A borrowed capability cannot be destroyed through MaintenanceParticipant*.
    ~MaintenanceParticipant() = default;
};

} // namespace Maintenance
} // namespace AquaCore
