#include <stdint.h>
#include <unity.h>

#include "AquaCore/Maintenance/MaintenanceTransition.h"

namespace {

using AquaCore::Maintenance::MaintenanceTransitionDecision;
using AquaCore::Maintenance::MaintenanceTransitionRequest;
using AquaCore::Maintenance::evaluateMaintenanceTransition;
using AquaCore::System::OperationalState;

void testEnterMaintenanceTransitionMatrix() {
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::Allowed,
                      evaluateMaintenanceTransition(OperationalState::RUNNING,
                                                    MaintenanceTransitionRequest::Enter));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::AlreadyInTargetState,
                      evaluateMaintenanceTransition(OperationalState::MAINTENANCE,
                                                    MaintenanceTransitionRequest::Enter));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(OperationalState::BOOTING,
                                                    MaintenanceTransitionRequest::Enter));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(OperationalState::ERROR,
                                                    MaintenanceTransitionRequest::Enter));
}

void testExitMaintenanceTransitionMatrix() {
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::Allowed,
                      evaluateMaintenanceTransition(OperationalState::MAINTENANCE,
                                                    MaintenanceTransitionRequest::Exit));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::AlreadyInTargetState,
                      evaluateMaintenanceTransition(OperationalState::RUNNING,
                                                    MaintenanceTransitionRequest::Exit));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(OperationalState::BOOTING,
                                                    MaintenanceTransitionRequest::Exit));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(OperationalState::ERROR,
                                                    MaintenanceTransitionRequest::Exit));
}

void testInvalidMaintenanceInputsFailClosed() {
    const MaintenanceTransitionRequest invalidRequest =
        static_cast<MaintenanceTransitionRequest>(0xFFU);
    const OperationalState invalidState = static_cast<OperationalState>(0xFFU);
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidRequest,
                      evaluateMaintenanceTransition(OperationalState::RUNNING, invalidRequest));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidRequest,
                      evaluateMaintenanceTransition(invalidState, invalidRequest));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(invalidState,
                                                    MaintenanceTransitionRequest::Enter));
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::InvalidState,
                      evaluateMaintenanceTransition(invalidState,
                                                    MaintenanceTransitionRequest::Exit));
}

void testMaintenanceGuardDoesNotChangeState() {
    OperationalState current = OperationalState::RUNNING;
    const MaintenanceTransitionDecision first =
        evaluateMaintenanceTransition(current, MaintenanceTransitionRequest::Enter);
    const MaintenanceTransitionDecision second =
        evaluateMaintenanceTransition(current, MaintenanceTransitionRequest::Enter);
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::Allowed, first);
    TEST_ASSERT_EQUAL(first, second);
    TEST_ASSERT_EQUAL(OperationalState::RUNNING, current);

    current = OperationalState::MAINTENANCE;
    TEST_ASSERT_EQUAL(MaintenanceTransitionDecision::Allowed,
                      evaluateMaintenanceTransition(current, MaintenanceTransitionRequest::Exit));
    TEST_ASSERT_EQUAL(OperationalState::MAINTENANCE, current);
}

} // namespace

void runMaintenanceTransitionTests() {
    RUN_TEST(testEnterMaintenanceTransitionMatrix);
    RUN_TEST(testExitMaintenanceTransitionMatrix);
    RUN_TEST(testInvalidMaintenanceInputsFailClosed);
    RUN_TEST(testMaintenanceGuardDoesNotChangeState);
}
