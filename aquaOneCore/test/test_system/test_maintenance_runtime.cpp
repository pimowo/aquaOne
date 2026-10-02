#include <unity.h>

#include "AquaCore/System/ApplicationRuntime.h"
#include "AquaCore/System/RuntimeStateCoordinator.h"

namespace {

namespace Maintenance = AquaCore::Maintenance;
namespace System = AquaCore::System;

System::StartupStepResult succeed(void*) {
    return System::StartupStepResult::succeeded();
}

System::ApplicationPlan plan() {
    System::ApplicationPlan value {};
    value.earlySafeOutputs = {"early", succeed, nullptr};
    value.safetyGate = {"gate", succeed, nullptr};
    return value;
}

class Health final : public System::HealthProvider {
public:
    System::HealthState state = System::HealthState::OK;
    System::HealthState healthContribution() const override { return state; }
};

class Safety final : public System::SafetyProvider {
public:
    System::SafetyState state = System::SafetyState::CLEAR;
    System::SafetyState safetyContribution() const override { return state; }
};

class Participant final : public Maintenance::MaintenanceParticipant {
public:
    System::ApplicationRuntime* runtime = nullptr;
    Maintenance::MaintenanceParticipantResult enter =
        Maintenance::MaintenanceParticipantResult::Prepared;
    Maintenance::MaintenanceParticipantResult exit =
        Maintenance::MaintenanceParticipantResult::Prepared;
    System::OperationalState observed = System::OperationalState::BOOTING;
    unsigned enterCalls = 0U;
    unsigned exitCalls = 0U;

    Maintenance::MaintenanceParticipantResult prepareEnter() override {
        ++enterCalls;
        observed = runtime->status().operational;
        return enter;
    }

    Maintenance::MaintenanceParticipantResult prepareExit() override {
        ++exitCalls;
        observed = runtime->status().operational;
        return exit;
    }
};

struct Fixture {
    Health health;
    Safety safety;
    const System::HealthProvider* healthProviders[1];
    const System::SafetyProvider* safetyProviders[1];
    System::RuntimeStateCoordinator coordinator;
    System::ApplicationRuntime runtime;
    Participant participant;

    Fixture() : healthProviders {&health},
        safetyProviders {&safety},
        coordinator(healthProviders, 1U, safetyProviders, 1U),
        runtime(plan(), {nullptr, 0U}) {
        participant.runtime = &runtime;
    }

    void startAndHandoff() {
        TEST_ASSERT_FALSE(runtime.start().hasFatalFailure());
        TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    }

    void assertLive(System::OperationalState operational,
                    System::HealthState expectedHealth,
                    System::SafetyState expectedSafety) const {
        const System::RuntimeStatus value = runtime.status();
        TEST_ASSERT_EQUAL_INT(static_cast<int>(operational),
                              static_cast<int>(value.operational));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expectedHealth),
                              static_cast<int>(value.health));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expectedSafety),
                              static_cast<int>(value.safety));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(System::StartupPhase::RUNNING),
                              static_cast<int>(value.startupPhase));
    }
};

using Outcome = Maintenance::MaintenanceTransitionOutcome;
using Request = Maintenance::MaintenanceTransitionRequest;
using Result = Maintenance::MaintenanceParticipantResult;

void assertOutcome(Outcome expected, Outcome actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void testPreparedEnterAndExitCommitOnlyAfterPreparation() {
    Fixture fixture;
    fixture.startAndHandoff();
    fixture.health.state = System::HealthState::DEGRADED;
    fixture.safety.state = System::SafetyState::LOCKED;
    fixture.coordinator.refresh();

    assertOutcome(Outcome::Completed,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::RUNNING),
                          static_cast<int>(fixture.participant.observed));
    fixture.assertLive(System::OperationalState::MAINTENANCE,
        System::HealthState::DEGRADED, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);

    assertOutcome(Outcome::Completed,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::MAINTENANCE),
                          static_cast<int>(fixture.participant.observed));
    fixture.assertLive(System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.exitCalls);
}

void testSameTargetDoesNotCallParticipantOrWriteStatus() {
    Fixture fixture;
    fixture.startAndHandoff();
    const System::RuntimeStatus running = fixture.runtime.status();
    assertOutcome(Outcome::AlreadyInTargetState,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    const System::RuntimeStatus afterExit = fixture.runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&running, &afterExit, sizeof(running));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);

    assertOutcome(Outcome::Completed,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    const System::RuntimeStatus maintenance = fixture.runtime.status();
    assertOutcome(Outcome::AlreadyInTargetState,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    const System::RuntimeStatus afterEnter = fixture.runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&maintenance, &afterEnter, sizeof(maintenance));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);
}

void testBootingAndPreHandoffCannotTransition() {
    Fixture fixture;
    const Request requests[] = {Request::Enter, Request::Exit};
    for (Request request : requests) {
        assertOutcome(Outcome::InvalidState,
            fixture.runtime.requestMaintenanceTransition(request, fixture.participant));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::BOOTING),
                          static_cast<int>(fixture.runtime.status().operational));
    fixture.runtime.start();
    assertOutcome(Outcome::InvalidState,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    assertOutcome(Outcome::InvalidState,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);
    TEST_ASSERT_FALSE(fixture.runtime.runtimeStateHandedOff());
}

void testInvalidRequestDoesNotCallParticipantOrMutate() {
    Fixture fixture;
    fixture.startAndHandoff();
    const System::RuntimeStatus before = fixture.runtime.status();
    assertOutcome(Outcome::InvalidRequest,
        fixture.runtime.requestMaintenanceTransition(static_cast<Request>(0xFFU),
                                                     fixture.participant));
    const System::RuntimeStatus after = fixture.runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);
}

void testRejectedEnterAndExitPreserveSourceAndHealthSafety() {
    Fixture fixture;
    fixture.startAndHandoff();
    fixture.health.state = System::HealthState::DEGRADED;
    fixture.coordinator.refresh();
    fixture.participant.enter = Result::Rejected;
    const System::RuntimeStatus running = fixture.runtime.status();
    assertOutcome(Outcome::Rejected,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    const System::RuntimeStatus afterEnter = fixture.runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&running, &afterEnter, sizeof(running));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.enterCalls);
    fixture.health.state = System::HealthState::OK;
    fixture.coordinator.refresh();
    fixture.assertLive(System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);

    fixture.participant.enter = Result::Prepared;
    assertOutcome(Outcome::Completed,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    fixture.participant.exit = Result::Rejected;
    const System::RuntimeStatus maintenance = fixture.runtime.status();
    assertOutcome(Outcome::Rejected,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    const System::RuntimeStatus afterExit = fixture.runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&maintenance, &afterExit, sizeof(maintenance));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.exitCalls);
    fixture.health.state = System::HealthState::DEGRADED;
    fixture.coordinator.refresh();
    fixture.assertLive(System::OperationalState::MAINTENANCE,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
}

void assertFailurePersists(Fixture& fixture) {
    fixture.assertLive(System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
    TEST_ASSERT_TRUE(fixture.runtime.runtimeStateHandedOff());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
                          static_cast<int>(fixture.coordinator.refresh()));
    fixture.assertLive(System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
    const unsigned enterCalls = fixture.participant.enterCalls;
    const unsigned exitCalls = fixture.participant.exitCalls;
    assertOutcome(Outcome::InvalidState,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    assertOutcome(Outcome::InvalidState,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    assertOutcome(Outcome::InvalidRequest,
        fixture.runtime.requestMaintenanceTransition(static_cast<Request>(0xFFU),
                                                     fixture.participant));
    TEST_ASSERT_EQUAL_UINT(enterCalls, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(exitCalls, fixture.participant.exitCalls);
    const System::StartupReport& report = fixture.runtime.startupReport();
    TEST_ASSERT_FALSE(report.hasFatalFailure());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::RUNNING),
                          static_cast<int>(report.finalStatus().operational));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::HealthState::OK),
                          static_cast<int>(report.finalStatus().health));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::SafetyState::CLEAR),
                          static_cast<int>(report.finalStatus().safety));
}

void testFailedEnterLatchesFailSafeAfterHandoff() {
    Fixture fixture;
    fixture.startAndHandoff();
    fixture.participant.enter = Result::Failed;
    assertOutcome(Outcome::Failed,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.participant.exitCalls);
    assertFailurePersists(fixture);
}

void testFailedExitLatchesFailSafeAfterHandoff() {
    Fixture fixture;
    fixture.startAndHandoff();
    assertOutcome(Outcome::Completed,
        fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant));
    fixture.health.state = System::HealthState::DEGRADED;
    fixture.safety.state = System::SafetyState::LOCKED;
    fixture.coordinator.refresh();
    fixture.participant.exit = Result::Failed;
    assertOutcome(Outcome::Failed,
        fixture.runtime.requestMaintenanceTransition(Request::Exit, fixture.participant));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.participant.exitCalls);
    assertFailurePersists(fixture);
}

void testInvalidParticipantResultsLatchFailSafeInBothDirections() {
    Fixture enter;
    enter.startAndHandoff();
    enter.participant.enter = static_cast<Result>(0xFFU);
    assertOutcome(Outcome::InvalidParticipantResult,
        enter.runtime.requestMaintenanceTransition(Request::Enter, enter.participant));
    assertFailurePersists(enter);

    Fixture exit;
    exit.startAndHandoff();
    assertOutcome(Outcome::Completed,
        exit.runtime.requestMaintenanceTransition(Request::Enter, exit.participant));
    exit.participant.exit = static_cast<Result>(0xFFU);
    assertOutcome(Outcome::InvalidParticipantResult,
        exit.runtime.requestMaintenanceTransition(Request::Exit, exit.participant));
    assertFailurePersists(exit);
}

void testProviderRefreshStillWorksInRunningAndMaintenance() {
    Fixture fixture;
    fixture.startAndHandoff();
    fixture.health.state = System::HealthState::DEGRADED;
    fixture.coordinator.refresh();
    fixture.assertLive(System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
    fixture.runtime.requestMaintenanceTransition(Request::Enter, fixture.participant);
    fixture.health.state = System::HealthState::OK;
    fixture.safety.state = System::SafetyState::LOCKED;
    fixture.coordinator.refresh();
    fixture.assertLive(System::OperationalState::MAINTENANCE,
        System::HealthState::OK, System::SafetyState::LOCKED);
}

} // namespace

void runMaintenanceRuntimeTests() {
    RUN_TEST(testPreparedEnterAndExitCommitOnlyAfterPreparation);
    RUN_TEST(testSameTargetDoesNotCallParticipantOrWriteStatus);
    RUN_TEST(testBootingAndPreHandoffCannotTransition);
    RUN_TEST(testInvalidRequestDoesNotCallParticipantOrMutate);
    RUN_TEST(testRejectedEnterAndExitPreserveSourceAndHealthSafety);
    RUN_TEST(testFailedEnterLatchesFailSafeAfterHandoff);
    RUN_TEST(testFailedExitLatchesFailSafeAfterHandoff);
    RUN_TEST(testInvalidParticipantResultsLatchFailSafeInBothDirections);
    RUN_TEST(testProviderRefreshStillWorksInRunningAndMaintenance);
}
