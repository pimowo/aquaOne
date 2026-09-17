#include "AquaCore/System/SystemState.h"

namespace AquaCore {
namespace System {

const char* operationalStateName(OperationalState state) {
    switch (state) {
        case OperationalState::BOOTING:
            return "BOOTING";
        case OperationalState::RUNNING:
            return "RUNNING";
        case OperationalState::MAINTENANCE:
            return "MAINTENANCE";
        case OperationalState::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

const char* healthStateName(HealthState state) {
    switch (state) {
        case HealthState::OK:
            return "OK";
        case HealthState::DEGRADED:
            return "DEGRADED";
        case HealthState::FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

const char* safetyStateName(SafetyState state) {
    switch (state) {
        case SafetyState::CLEAR:
            return "CLEAR";
        case SafetyState::LOCKED:
            return "LOCKED";
        default:
            return "UNKNOWN";
    }
}

const char* startupPhaseName(StartupPhase phase) {
    switch (phase) {
        case StartupPhase::BOOT:
            return "BOOT";
        case StartupPhase::CORE_INIT:
            return "CORE_INIT";
        case StartupPhase::LOAD_VALIDATE_CONFIG:
            return "LOAD_VALIDATE_CONFIG";
        case StartupPhase::HARDWARE_INIT:
            return "HARDWARE_INIT";
        case StartupPhase::DOMAIN_INIT:
            return "DOMAIN_INIT";
        case StartupPhase::SAFETY_VALIDATION:
            return "SAFETY_VALIDATION";
        case StartupPhase::NETWORK_INIT:
            return "NETWORK_INIT";
        case StartupPhase::INTERFACES_INIT:
            return "INTERFACES_INIT";
        case StartupPhase::RUNNING:
            return "RUNNING";
        default:
            return "UNKNOWN";
    }
}

} // namespace System
} // namespace AquaCore