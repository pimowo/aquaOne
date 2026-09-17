#include <stdint.h>
#include <type_traits>

#include <unity.h>

#include "AquaCore/System/SystemState.h"

namespace {

using AquaCore::System::HealthState;
using AquaCore::System::OperationalState;
using AquaCore::System::SafetyState;
using AquaCore::System::StartupPhase;

static_assert(std::is_enum<OperationalState>::value,
              "OperationalState must be an enum");
static_assert(std::is_enum<HealthState>::value,
              "HealthState must be an enum");
static_assert(std::is_enum<SafetyState>::value,
              "SafetyState must be an enum");
static_assert(std::is_enum<StartupPhase>::value,
              "StartupPhase must be an enum");

static_assert(!std::is_same<OperationalState, HealthState>::value,
              "OperationalState and HealthState must be independent types");
static_assert(!std::is_same<OperationalState, SafetyState>::value,
              "OperationalState and SafetyState must be independent types");
static_assert(!std::is_same<OperationalState, StartupPhase>::value,
              "OperationalState and StartupPhase must be independent types");
static_assert(!std::is_same<HealthState, SafetyState>::value,
              "HealthState and SafetyState must be independent types");
static_assert(!std::is_same<HealthState, StartupPhase>::value,
              "HealthState and StartupPhase must be independent types");
static_assert(!std::is_same<SafetyState, StartupPhase>::value,
              "SafetyState and StartupPhase must be independent types");
static_assert(!std::is_convertible<OperationalState, HealthState>::value &&
                  !std::is_convertible<OperationalState, SafetyState>::value &&
                  !std::is_convertible<OperationalState, StartupPhase>::value &&
                  !std::is_convertible<HealthState, SafetyState>::value &&
                  !std::is_convertible<HealthState, StartupPhase>::value &&
                  !std::is_convertible<SafetyState, StartupPhase>::value,
              "System state axes must not be implicitly convertible");
static_assert(!std::is_convertible<OperationalState, uint8_t>::value &&
                  !std::is_convertible<HealthState, uint8_t>::value &&
                  !std::is_convertible<SafetyState, uint8_t>::value &&
                  !std::is_convertible<StartupPhase, uint8_t>::value,
              "System state types must remain scoped enums");

void testOperationalStateNames() {
    TEST_ASSERT_EQUAL_STRING("BOOTING", AquaCore::System::operationalStateName(OperationalState::BOOTING));
    TEST_ASSERT_EQUAL_STRING("RUNNING", AquaCore::System::operationalStateName(OperationalState::RUNNING));
    TEST_ASSERT_EQUAL_STRING("MAINTENANCE", AquaCore::System::operationalStateName(OperationalState::MAINTENANCE));
    TEST_ASSERT_EQUAL_STRING("ERROR", AquaCore::System::operationalStateName(OperationalState::ERROR));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", AquaCore::System::operationalStateName(static_cast<OperationalState>(0xFFU)));
}

void testHealthStateNames() {
    TEST_ASSERT_EQUAL_STRING("OK", AquaCore::System::healthStateName(HealthState::OK));
    TEST_ASSERT_EQUAL_STRING("DEGRADED", AquaCore::System::healthStateName(HealthState::DEGRADED));
    TEST_ASSERT_EQUAL_STRING("FAULT", AquaCore::System::healthStateName(HealthState::FAULT));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", AquaCore::System::healthStateName(static_cast<HealthState>(0xFFU)));
}

void testSafetyStateNames() {
    TEST_ASSERT_EQUAL_STRING("CLEAR", AquaCore::System::safetyStateName(SafetyState::CLEAR));
    TEST_ASSERT_EQUAL_STRING("LOCKED", AquaCore::System::safetyStateName(SafetyState::LOCKED));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", AquaCore::System::safetyStateName(static_cast<SafetyState>(0xFFU)));
}

void testStartupPhaseNames() {
    TEST_ASSERT_EQUAL_STRING("BOOT", AquaCore::System::startupPhaseName(StartupPhase::BOOT));
    TEST_ASSERT_EQUAL_STRING("CORE_INIT", AquaCore::System::startupPhaseName(StartupPhase::CORE_INIT));
    TEST_ASSERT_EQUAL_STRING("LOAD_VALIDATE_CONFIG", AquaCore::System::startupPhaseName(StartupPhase::LOAD_VALIDATE_CONFIG));
    TEST_ASSERT_EQUAL_STRING("HARDWARE_INIT", AquaCore::System::startupPhaseName(StartupPhase::HARDWARE_INIT));
    TEST_ASSERT_EQUAL_STRING("DOMAIN_INIT", AquaCore::System::startupPhaseName(StartupPhase::DOMAIN_INIT));
    TEST_ASSERT_EQUAL_STRING("SAFETY_VALIDATION", AquaCore::System::startupPhaseName(StartupPhase::SAFETY_VALIDATION));
    TEST_ASSERT_EQUAL_STRING("NETWORK_INIT", AquaCore::System::startupPhaseName(StartupPhase::NETWORK_INIT));
    TEST_ASSERT_EQUAL_STRING("INTERFACES_INIT", AquaCore::System::startupPhaseName(StartupPhase::INTERFACES_INIT));
    TEST_ASSERT_EQUAL_STRING("RUNNING", AquaCore::System::startupPhaseName(StartupPhase::RUNNING));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", AquaCore::System::startupPhaseName(static_cast<StartupPhase>(0xFFU)));
}

} // namespace

void runSystemStateTests() {
    RUN_TEST(testOperationalStateNames);
    RUN_TEST(testHealthStateNames);
    RUN_TEST(testSafetyStateNames);
    RUN_TEST(testStartupPhaseNames);
}