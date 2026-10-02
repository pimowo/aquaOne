#pragma once

#include <stdint.h>

#include "AquaCore/System/SystemState.h"

namespace AquaCore {
namespace Maintenance {

enum class NormalProcessingDecision : uint8_t {
    Allowed,
    Blocked
};

// Applies only to normal autonomous Domain processing. Intrinsic safety is
// outside this gate and follows its own Domain/hardware safety semantics.
inline NormalProcessingDecision evaluateNormalProcessing(
    System::OperationalState state
) {
    return state == System::OperationalState::RUNNING
        ? NormalProcessingDecision::Allowed
        : NormalProcessingDecision::Blocked;
}

} // namespace Maintenance
} // namespace AquaCore
