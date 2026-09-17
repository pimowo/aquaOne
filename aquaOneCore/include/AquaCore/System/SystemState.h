#pragma once

#include <stdint.h>

namespace AquaCore {
namespace System {

enum class OperationalState : uint8_t {
    BOOTING,
    RUNNING,
    MAINTENANCE,
    ERROR
};

enum class HealthState : uint8_t {
    OK,
    DEGRADED,
    FAULT
};

enum class SafetyState : uint8_t {
    CLEAR,
    LOCKED
};

enum class StartupPhase : uint8_t {
    BOOT,
    CORE_INIT,
    LOAD_VALIDATE_CONFIG,
    HARDWARE_INIT,
    DOMAIN_INIT,
    SAFETY_VALIDATION,
    NETWORK_INIT,
    INTERFACES_INIT,
    RUNNING
};

const char* operationalStateName(OperationalState state);
const char* healthStateName(HealthState state);
const char* safetyStateName(SafetyState state);
const char* startupPhaseName(StartupPhase phase);

} // namespace System
} // namespace AquaCore