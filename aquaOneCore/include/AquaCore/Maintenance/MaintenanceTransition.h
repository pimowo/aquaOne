#pragma once

#include <stdint.h>

#include "AquaCore/System/SystemState.h"

namespace AquaCore {
namespace Maintenance {

enum class MaintenanceTransitionRequest : uint8_t {
    Enter,
    Exit
};

enum class MaintenanceTransitionDecision : uint8_t {
    Allowed,
    AlreadyInTargetState,
    InvalidState,
    InvalidRequest
};

// Result of the runtime transition, distinct from participant preparation.
enum class MaintenanceTransitionOutcome : uint8_t {
    Completed,
    AlreadyInTargetState,
    Rejected,
    Failed,
    InvalidState,
    InvalidRequest,
    InvalidParticipantResult
};

// Legality of starting a transition only. This pure guard does not perform
// preparation, change OperationalState, or establish that the target was reached.
inline MaintenanceTransitionDecision evaluateMaintenanceTransition(
    System::OperationalState current,
    MaintenanceTransitionRequest request
) {
    switch (request) {
        case MaintenanceTransitionRequest::Enter:
            switch (current) {
                case System::OperationalState::RUNNING:
                    return MaintenanceTransitionDecision::Allowed;
                case System::OperationalState::MAINTENANCE:
                    return MaintenanceTransitionDecision::AlreadyInTargetState;
                default:
                    return MaintenanceTransitionDecision::InvalidState;
            }
        case MaintenanceTransitionRequest::Exit:
            switch (current) {
                case System::OperationalState::MAINTENANCE:
                    return MaintenanceTransitionDecision::Allowed;
                case System::OperationalState::RUNNING:
                    return MaintenanceTransitionDecision::AlreadyInTargetState;
                default:
                    return MaintenanceTransitionDecision::InvalidState;
            }
        default:
            return MaintenanceTransitionDecision::InvalidRequest;
    }
}

} // namespace Maintenance
} // namespace AquaCore
