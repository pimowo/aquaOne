#include <type_traits>

#include <unity.h>
#include "AquaCore/System/ApplicationRuntime.h"

namespace {

namespace System = AquaCore::System;

static_assert(!std::is_copy_constructible<System::ApplicationRuntime>::value,
    "ApplicationRuntime must not be copy constructible");
static_assert(!std::is_copy_assignable<System::ApplicationRuntime>::value,
    "ApplicationRuntime must not be copy assignable");
static_assert(!std::is_move_constructible<System::ApplicationRuntime>::value,
    "ApplicationRuntime must not be move constructible");
static_assert(!std::is_move_assignable<System::ApplicationRuntime>::value,
    "ApplicationRuntime must not be move assignable");

enum class CallbackMode {
    SUCCESS,
    DISABLED,
    FAILED,
    INVALID_MISSING_CODE,
    INVALID_MALFORMED_CODE
};

struct Trace {
    int values[24];
    size_t count;

    Trace() : values {}, count(0U) {
    }
};

struct CallbackContext {
    CallbackMode mode;
    int calls;
    Trace* trace;
    int marker;

    explicit CallbackContext(
        CallbackMode value = CallbackMode::SUCCESS,
        Trace* traceValue = nullptr,
        int markerValue = 0
    ) : mode(value), calls(0), trace(traceValue), marker(markerValue) {
    }
};

System::StartupStepResult callback(void* rawContext) {
    CallbackContext* context = static_cast<CallbackContext*>(rawContext);
    ++context->calls;
    if (context->trace != nullptr) {
        context->trace->values[context->trace->count++] = context->marker;
    }
    switch (context->mode) {
        case CallbackMode::SUCCESS:
            return System::StartupStepResult::succeeded();
        case CallbackMode::DISABLED:
            return System::StartupStepResult::disabled();
        case CallbackMode::FAILED:
            return System::StartupStepResult::failed(
                System::StartupErrorCode::fromStatic(
                    "AQUA.TEST", "CALLBACK_FAILED"
                )
            );
        case CallbackMode::INVALID_MISSING_CODE:
            return System::StartupStepResult::failed(
                System::StartupErrorCode::fromStatic(nullptr, nullptr)
            );
        case CallbackMode::INVALID_MALFORMED_CODE:
        default:
            return System::StartupStepResult::failed(
                System::StartupErrorCode::fromStatic(
                    "AQUA.test", "CALLBACK_FAILED"
                )
            );
    }
}

System::StartupAction action(const char* id, CallbackContext& context) {
    System::StartupAction value {};
    value.participantId = id;
    value.callback = callback;
    value.context = &context;
    return value;
}

System::StartupParticipant participant(
    const char* id,
    CallbackContext& context,
    System::StartupPhase phase,
    System::StartupRequirement requirement
) {
    System::StartupParticipant value {};
    value.action = action(id, context);
    value.phase = phase;
    value.requirement = requirement;
    return value;
}

System::ApplicationPlan plan(
    CallbackContext& early,
    CallbackContext& gate,
    const System::StartupParticipant* participants,
    size_t participantCount
) {
    System::ApplicationPlan value {};
    value.earlySafeOutputs = action("early", early);
    value.safetyGate = action("gate", gate);
    value.participants = participants;
    value.participantCount = participantCount;
    return value;
}

System::StartupFailureStorage noFailureStorage() {
    System::StartupFailureStorage value {};
    value.records = nullptr;
    value.capacity = 0U;
    return value;
}

void assertStatus(
    const System::RuntimeStatus& status,
    System::OperationalState operational,
    System::HealthState health,
    System::SafetyState safety,
    System::StartupPhase phase
) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(operational),
        static_cast<int>(status.operational)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(health),
        static_cast<int>(status.health)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(safety),
        static_cast<int>(status.safety)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(phase),
        static_cast<int>(status.startupPhase)
    );
}

void assertFatalCode(
    const System::StartupReport& report,
    System::StartupFailureKind kind,
    System::StartupFailureSource source,
    const System::StartupErrorCode& code
) {
    TEST_ASSERT_TRUE(report.isComplete());
    TEST_ASSERT_TRUE(report.hasFatalFailure());
    TEST_ASSERT_NOT_NULL(report.fatalFailure());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(kind),
        static_cast<int>(report.fatalFailure()->kind())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(source),
        static_cast<int>(report.fatalFailure()->source())
    );
    TEST_ASSERT_TRUE(report.fatalFailure()->errorCode().equals(code));
}

void testCoreStartupErrorCatalogIsStable() {
    struct ExpectedCode {
        System::StartupErrorCode (*factory)();
        const char* localCode;
    };
    const ExpectedCode expected[] = {
        { System::CoreStartupError::participantStorageInvalid,
            "PARTICIPANT_STORAGE_INVALID" },
        { System::CoreStartupError::reportStorageInvalid,
            "REPORT_STORAGE_INVALID" },
        { System::CoreStartupError::callbackMissing, "CALLBACK_MISSING" },
        { System::CoreStartupError::participantIdMissing,
            "PARTICIPANT_ID_MISSING" },
        { System::CoreStartupError::participantIdDuplicate,
            "PARTICIPANT_ID_DUPLICATE" },
        { System::CoreStartupError::phaseInvalid, "PHASE_INVALID" },
        { System::CoreStartupError::phaseNotAllowed, "PHASE_NOT_ALLOWED" },
        { System::CoreStartupError::phaseOrderInvalid,
            "PHASE_ORDER_INVALID" },
        { System::CoreStartupError::requirementInvalid,
            "REQUIREMENT_INVALID" },
        { System::CoreStartupError::outcomeInvalid, "OUTCOME_INVALID" },
        { System::CoreStartupError::errorCodeMissing, "ERROR_CODE_MISSING" },
        { System::CoreStartupError::errorCodeInvalid, "ERROR_CODE_INVALID" },
        { System::CoreStartupError::errorCodeUnexpected,
            "ERROR_CODE_UNEXPECTED" },
        { System::CoreStartupError::requiredDisabled, "REQUIRED_DISABLED" },
        { System::CoreStartupError::specialDisabled, "SPECIAL_DISABLED" }
    };
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        const System::StartupErrorCode code = expected[index].factory();
        TEST_ASSERT_TRUE(code.isValid());
        TEST_ASSERT_EQUAL_STRING("AQUA.CORE", code.ownerNamespace());
        TEST_ASSERT_EQUAL_STRING(expected[index].localCode, code.localCode());
    }
}

void testRuntimeReportIsIncompleteBeforeStart() {
    CallbackContext early;
    CallbackContext gate;
    System::ApplicationRuntime runtime(
        plan(early, gate, nullptr, 0U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.startupReport();
    TEST_ASSERT_FALSE(report.isComplete());
    TEST_ASSERT_FALSE(report.hasFatalFailure());
    TEST_ASSERT_NULL(report.fatalFailure());
    TEST_ASSERT_EQUAL_UINT(0U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(0U, report.storedOptionalFailureCount());
    TEST_ASSERT_FALSE(report.optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_UINT(0U, report.totalFailureCount());
    assertStatus(runtime.status(), System::OperationalState::BOOTING,
        System::HealthState::OK, System::SafetyState::LOCKED,
        System::StartupPhase::BOOT);
}

void testRuntimeSuccessAndSecondStartAreOneShot() {
    CallbackContext early;
    CallbackContext gate;
    System::ApplicationRuntime runtime(
        plan(early, gate, nullptr, 0U), noFailureStorage()
    );
    const System::StartupReport* first = &runtime.start();
    const System::RuntimeStatus firstStatus = runtime.status();
    const System::StartupReport* second = &runtime.start();
    const System::RuntimeStatus secondStatus = runtime.status();
    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_INT(1, early.calls);
    TEST_ASSERT_EQUAL_INT(1, gate.calls);
    TEST_ASSERT_EQUAL_MEMORY(&firstStatus, &secondStatus, sizeof(firstStatus));
    TEST_ASSERT_FALSE(first->hasFatalFailure());
    assertStatus(first->finalStatus(), System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING);
}

void testPlanRejectsInvalidParticipantPointerCountRelations() {
    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext ordinary;
    System::StartupParticipant unused = participant("unused", ordinary,
        System::StartupPhase::BOOT, System::StartupRequirement::REQUIRED);
    System::ApplicationRuntime first(
        plan(earlyA, gateA, &unused, 0U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PLAN,
        System::CoreStartupError::participantStorageInvalid());

    CallbackContext earlyB;
    CallbackContext gateB;
    System::ApplicationRuntime second(
        plan(earlyB, gateB, nullptr, 1U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PLAN,
        System::CoreStartupError::participantStorageInvalid());
}

void testPlanRejectsInvalidReportStorageRelationsAfterEarlySafe() {
    CallbackContext earlyA;
    CallbackContext gateA;
    System::StartupFailureRecord unused[1];
    System::StartupFailureStorage zeroWithPointer { unused, 0U };
    System::ApplicationRuntime first(
        plan(earlyA, gateA, nullptr, 0U), zeroWithPointer
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PLAN,
        System::CoreStartupError::reportStorageInvalid());
    TEST_ASSERT_EQUAL_INT(1, earlyA.calls);

    CallbackContext earlyB;
    CallbackContext gateB;
    System::StartupFailureStorage countWithoutPointer { nullptr, 1U };
    System::ApplicationRuntime second(
        plan(earlyB, gateB, nullptr, 0U), countWithoutPointer
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PLAN,
        System::CoreStartupError::reportStorageInvalid());
    TEST_ASSERT_EQUAL_INT(1, earlyB.calls);
}

void testPlanRejectsMissingEarlyCallbackAndIdBeforeCallingAnything() {
    CallbackContext earlyA;
    CallbackContext gateA;
    System::ApplicationPlan missingCallback = plan(earlyA, gateA, nullptr, 0U);
    missingCallback.earlySafeOutputs.callback = nullptr;
    System::ApplicationRuntime first(missingCallback, noFailureStorage());
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::EARLY_SAFE_OUTPUTS,
        System::CoreStartupError::callbackMissing());
    TEST_ASSERT_EQUAL_INT(0, earlyA.calls);
    TEST_ASSERT_EQUAL_INT(0, gateA.calls);

    CallbackContext earlyB;
    CallbackContext gateB;
    System::ApplicationPlan missingId = plan(earlyB, gateB, nullptr, 0U);
    missingId.earlySafeOutputs.participantId = "";
    System::ApplicationRuntime second(missingId, noFailureStorage());
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::EARLY_SAFE_OUTPUTS,
        System::CoreStartupError::participantIdMissing());
    TEST_ASSERT_EQUAL_INT(0, earlyB.calls);
}

void testPlanRejectsMissingSafetyCallbackAfterEarlySafe() {
    CallbackContext early;
    CallbackContext gate;
    System::ApplicationPlan value = plan(early, gate, nullptr, 0U);
    value.safetyGate.callback = nullptr;
    System::ApplicationRuntime runtime(value, noFailureStorage());
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::SAFETY_GATE,
        System::CoreStartupError::callbackMissing());
    TEST_ASSERT_EQUAL_INT(1, early.calls);
    TEST_ASSERT_EQUAL_INT(0, gate.calls);
}

void testPlanRejectsMissingSafetyGateIdAfterEarlySafe() {
    CallbackContext early;
    CallbackContext gate;
    System::ApplicationPlan value = plan(early, gate, nullptr, 0U);
    value.safetyGate.participantId = nullptr;
    System::ApplicationRuntime runtime(value, noFailureStorage());
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::SAFETY_GATE,
        System::CoreStartupError::participantIdMissing());
    TEST_ASSERT_EQUAL_INT(1, early.calls);
    TEST_ASSERT_EQUAL_INT(0, gate.calls);
}

void testPlanRejectsDuplicateIdsAcrossAllActionKinds() {
    const char idA[] = "same";
    const char idB[] = "same";

    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext ordinaryA;
    System::StartupParticipant earlyDuplicate[1] = {
        participant(idB, ordinaryA, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationPlan planA = plan(
        earlyA, gateA, earlyDuplicate, 1U);
    planA.earlySafeOutputs.participantId = idA;
    System::ApplicationRuntime first(planA, noFailureStorage());
    const System::StartupReport& firstReport = first.start();
    assertFatalCode(firstReport,
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::participantIdDuplicate());
    TEST_ASSERT_TRUE(firstReport.fatalFailure()->hasParticipantIndex());

    CallbackContext earlyB;
    CallbackContext gateB;
    CallbackContext ordinaryB;
    System::StartupParticipant gateDuplicate[1] = {
        participant(idB, ordinaryB, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationPlan planB = plan(
        earlyB, gateB, gateDuplicate, 1U);
    planB.safetyGate.participantId = idA;
    System::ApplicationRuntime second(planB, noFailureStorage());
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::participantIdDuplicate());

    CallbackContext earlyC;
    CallbackContext gateC;
    CallbackContext ordinaryC;
    CallbackContext ordinaryD;
    System::StartupParticipant ordinaryDuplicates[2] = {
        participant(idA, ordinaryC, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED),
        participant(idB, ordinaryD, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime third(
        plan(earlyC, gateC, ordinaryDuplicates, 2U),
        noFailureStorage());
    const System::StartupReport& thirdReport = third.start();
    assertFatalCode(thirdReport,
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::participantIdDuplicate());
    TEST_ASSERT_EQUAL_UINT(1U, thirdReport.fatalFailure()->participantIndex());
}

void testPlanRejectsMissingOrdinaryCallbackAndId() {
    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext ordinaryA;
    System::StartupParticipant missingCallback[1] = {
        participant("ordinary", ordinaryA, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED)
    };
    missingCallback[0].action.callback = nullptr;
    System::ApplicationRuntime first(
        plan(earlyA, gateA, missingCallback, 1U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::callbackMissing());

    CallbackContext earlyB;
    CallbackContext gateB;
    CallbackContext ordinaryB;
    System::StartupParticipant missingId[1] = {
        participant("", ordinaryB, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime second(
        plan(earlyB, gateB, missingId, 1U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::participantIdMissing());
}

void testPlanRejectsInvalidAndForbiddenPhases() {
    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext ordinaryA;
    System::StartupParticipant invalid[1] = {
        participant("invalid", ordinaryA,
            static_cast<System::StartupPhase>(0xFFU),
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime first(
        plan(earlyA, gateA, invalid, 1U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseInvalid());

    CallbackContext earlyB;
    CallbackContext gateB;
    CallbackContext ordinaryB;
    System::StartupParticipant forbidden[1] = {
        participant("forbidden", ordinaryB,
            System::StartupPhase::SAFETY_VALIDATION,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime second(
        plan(earlyB, gateB, forbidden, 1U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseNotAllowed());

    CallbackContext earlyC;
    CallbackContext gateC;
    CallbackContext runningParticipant;
    System::StartupParticipant running[1] = {
        participant("running", runningParticipant,
            System::StartupPhase::RUNNING,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime third(
        plan(earlyC, gateC, running, 1U), noFailureStorage());
    assertFatalCode(third.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseNotAllowed());
}

void testPlanRejectsInvalidRequirement() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext ordinary;
    System::StartupParticipant participants[1] = {
        participant("ordinary", ordinary, System::StartupPhase::BOOT,
            static_cast<System::StartupRequirement>(0xFFU))
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 1U), noFailureStorage()
    );
    assertFatalCode(runtime.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::requirementInvalid());
}

void testPlanRejectsRequiredNetworkAndInterfacesParticipants() {
    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext network;
    System::StartupParticipant networkParticipant[1] = {
        participant("network", network, System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime first(
        plan(earlyA, gateA, networkParticipant, 1U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseNotAllowed());

    CallbackContext earlyB;
    CallbackContext gateB;
    CallbackContext interfaces;
    System::StartupParticipant interfacesParticipant[1] = {
        participant("interfaces", interfaces,
            System::StartupPhase::INTERFACES_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime second(
        plan(earlyB, gateB, interfacesParticipant, 1U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseNotAllowed());
}

void testPlanRejectsDecreasingPhaseOrder() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext later;
    CallbackContext earlier;
    System::StartupParticipant participants[2] = {
        participant("later", later, System::StartupPhase::DOMAIN_INIT,
            System::StartupRequirement::REQUIRED),
        participant("earlier", earlier, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 2U), noFailureStorage()
    );
    assertFatalCode(runtime.start(),
        System::StartupFailureKind::PLAN_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::phaseOrderInvalid());
}

void testRuntimeExecutesFixedPhasesAndDeclarationOrder() {
    Trace trace;
    CallbackContext early(CallbackMode::SUCCESS, &trace, 1);
    CallbackContext bootA(CallbackMode::SUCCESS, &trace, 2);
    CallbackContext bootB(CallbackMode::SUCCESS, &trace, 3);
    CallbackContext core(CallbackMode::SUCCESS, &trace, 4);
    CallbackContext config(CallbackMode::SUCCESS, &trace, 5);
    CallbackContext hardware(CallbackMode::SUCCESS, &trace, 6);
    CallbackContext domain(CallbackMode::SUCCESS, &trace, 7);
    CallbackContext gate(CallbackMode::SUCCESS, &trace, 8);
    CallbackContext network(CallbackMode::SUCCESS, &trace, 9);
    CallbackContext interfaces(CallbackMode::SUCCESS, &trace, 10);
    System::StartupParticipant participants[8] = {
        participant("boot-a", bootA, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED),
        participant("boot-b", bootB, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED),
        participant("core", core, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::REQUIRED),
        participant("config", config,
            System::StartupPhase::LOAD_VALIDATE_CONFIG,
            System::StartupRequirement::REQUIRED),
        participant("hardware", hardware, System::StartupPhase::HARDWARE_INIT,
            System::StartupRequirement::REQUIRED),
        participant("domain", domain, System::StartupPhase::DOMAIN_INIT,
            System::StartupRequirement::REQUIRED),
        participant("network", network, System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL),
        participant("interfaces", interfaces,
            System::StartupPhase::INTERFACES_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 8U), noFailureStorage()
    );
    runtime.start();
    TEST_ASSERT_EQUAL_UINT(10U, trace.count);
    for (size_t index = 0U; index < trace.count; ++index) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(index + 1U), trace.values[index]);
    }
}

void testEarlyParticipantFailureIsFatalAndPreservesCode() {
    CallbackContext early(CallbackMode::FAILED);
    CallbackContext gate;
    System::ApplicationRuntime runtime(
        plan(early, gate, nullptr, 0U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PARTICIPANT_FAILURE,
        System::StartupFailureSource::EARLY_SAFE_OUTPUTS,
        System::StartupErrorCode::fromStatic(
            "AQUA.TEST", "CALLBACK_FAILED"));
    assertStatus(report.finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::BOOT);
    TEST_ASSERT_EQUAL_INT(0, gate.calls);
}

void testRequiredParticipantFailureIsFatalAndKeepsPhase() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext required(CallbackMode::FAILED);
    System::StartupParticipant participants[1] = {
        participant("required", required, System::StartupPhase::HARDWARE_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 1U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PARTICIPANT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::StartupErrorCode::fromStatic(
            "AQUA.TEST", "CALLBACK_FAILED"));
    TEST_ASSERT_TRUE(report.fatalFailure()->hasParticipantIndex());
    TEST_ASSERT_EQUAL_STRING("required", report.fatalFailure()->participantId());
    assertStatus(report.finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::HARDWARE_INIT);
    TEST_ASSERT_EQUAL_INT(0, gate.calls);
}

void testSafetyGateFailureIsFatalAndSafetyRemainsLocked() {
    CallbackContext early;
    CallbackContext gate(CallbackMode::FAILED);
    System::ApplicationRuntime runtime(
        plan(early, gate, nullptr, 0U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PARTICIPANT_FAILURE,
        System::StartupFailureSource::SAFETY_GATE,
        System::StartupErrorCode::fromStatic(
            "AQUA.TEST", "CALLBACK_FAILED"));
    TEST_ASSERT_FALSE(report.fatalFailure()->hasParticipantIndex());
    assertStatus(report.finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::SAFETY_VALIDATION);
}

void testLateInvalidResultReturnsClearToLockedAndStopsCallbacks() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext network(CallbackMode::INVALID_MALFORMED_CODE);
    CallbackContext interfaces;
    System::StartupParticipant participants[2] = {
        participant("network", network, System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL),
        participant("interfaces", interfaces,
            System::StartupPhase::INTERFACES_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 2U), noFailureStorage());
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::errorCodeInvalid());
    assertStatus(report.finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::NETWORK_INIT);
    TEST_ASSERT_EQUAL_INT(1, early.calls);
    TEST_ASSERT_EQUAL_INT(1, gate.calls);
    TEST_ASSERT_EQUAL_INT(1, network.calls);
    TEST_ASSERT_EQUAL_INT(0, interfaces.calls);
}

void testOrdinaryFailureRecordRetainsNonzeroIndexMetadata() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext first;
    CallbackContext failing(CallbackMode::FAILED);
    System::StartupParticipant participants[2] = {
        participant("first", first, System::StartupPhase::BOOT,
            System::StartupRequirement::REQUIRED),
        participant("failing", failing, System::StartupPhase::HARDWARE_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 2U), noFailureStorage());
    const System::StartupReport& report = runtime.start();
    assertFatalCode(report,
        System::StartupFailureKind::PARTICIPANT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::StartupErrorCode::fromStatic(
            "AQUA.TEST", "CALLBACK_FAILED"));
    const System::StartupFailureRecord* failure = report.fatalFailure();
    TEST_ASSERT_EQUAL_STRING("failing", failure->participantId());
    TEST_ASSERT_TRUE(failure->hasParticipantIndex());
    TEST_ASSERT_EQUAL_UINT(1U, failure->participantIndex());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupPhase::HARDWARE_INIT),
        static_cast<int>(failure->phase()));
}

void testRequiredDisabledAndSpecialDisabledAreContractFailures() {
    CallbackContext earlyA;
    CallbackContext gateA;
    CallbackContext required(CallbackMode::DISABLED);
    System::StartupParticipant participants[1] = {
        participant("required", required, System::StartupPhase::DOMAIN_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::ApplicationRuntime first(
        plan(earlyA, gateA, participants, 1U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::requiredDisabled());

    CallbackContext earlyB(CallbackMode::DISABLED);
    CallbackContext gateB;
    System::ApplicationRuntime second(
        plan(earlyB, gateB, nullptr, 0U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::EARLY_SAFE_OUTPUTS,
        System::CoreStartupError::specialDisabled());

    CallbackContext earlyC;
    CallbackContext gateC(CallbackMode::DISABLED);
    System::ApplicationRuntime third(
        plan(earlyC, gateC, nullptr, 0U), noFailureStorage()
    );
    assertFatalCode(third.start(),
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::SAFETY_GATE,
        System::CoreStartupError::specialDisabled());
}

void testOptionalDisabledDoesNotDegradeHealth() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext optional(CallbackMode::DISABLED);
    System::StartupParticipant participants[1] = {
        participant("optional", optional, System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 1U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();
    TEST_ASSERT_FALSE(report.hasFatalFailure());
    TEST_ASSERT_EQUAL_UINT(0U, report.totalFailureCount());
    assertStatus(report.finalStatus(), System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING);
}

void testInvalidCallbackResultsAreFatalContractFailures() {
    CallbackContext earlyA(CallbackMode::INVALID_MISSING_CODE);
    CallbackContext gateA;
    System::ApplicationRuntime first(
        plan(earlyA, gateA, nullptr, 0U), noFailureStorage()
    );
    assertFatalCode(first.start(),
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::EARLY_SAFE_OUTPUTS,
        System::CoreStartupError::errorCodeMissing());

    CallbackContext earlyB;
    CallbackContext gateB;
    CallbackContext ordinary(CallbackMode::INVALID_MALFORMED_CODE);
    System::StartupParticipant participants[1] = {
        participant("ordinary", ordinary, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::ApplicationRuntime second(
        plan(earlyB, gateB, participants, 1U), noFailureStorage()
    );
    assertFatalCode(second.start(),
        System::StartupFailureKind::RESULT_CONTRACT_FAILURE,
        System::StartupFailureSource::PARTICIPANT,
        System::CoreStartupError::errorCodeInvalid());
}

void testOptionalFailuresFillExactCapacityWithoutTruncation() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext firstFailure(CallbackMode::FAILED);
    CallbackContext secondFailure(CallbackMode::FAILED);
    System::StartupParticipant participants[2] = {
        participant("first", firstFailure, System::StartupPhase::BOOT,
            System::StartupRequirement::OPTIONAL),
        participant("second", secondFailure, System::StartupPhase::DOMAIN_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::StartupFailureRecord records[2];
    System::StartupFailureStorage storage { records, 2U };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 2U), storage
    );
    const System::StartupReport& report = runtime.start();
    TEST_ASSERT_EQUAL_UINT(2U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(2U, report.storedOptionalFailureCount());
    TEST_ASSERT_FALSE(report.optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_UINT(2U, report.totalFailureCount());
    TEST_ASSERT_EQUAL_PTR(records, report.optionalFailures());
    TEST_ASSERT_EQUAL_STRING("first", report.optionalFailure(0U)->participantId());
    TEST_ASSERT_EQUAL_STRING("second", report.optionalFailure(1U)->participantId());
    TEST_ASSERT_NULL(report.optionalFailure(2U));
    assertStatus(report.finalStatus(), System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING);
}

void testOptionalFailureWithZeroCapacityIsCountedAndTruncated() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext optional(CallbackMode::FAILED);
    System::StartupParticipant participants[1] = {
        participant("optional", optional, System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 1U), noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();
    TEST_ASSERT_EQUAL_UINT(1U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(0U, report.storedOptionalFailureCount());
    TEST_ASSERT_TRUE(report.optionalFailuresTruncated());
    TEST_ASSERT_NULL(report.optionalFailures());
    assertStatus(report.finalStatus(), System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING);
}

void testOptionalFailureOverflowPreservesFirstN() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext firstFailure(CallbackMode::FAILED);
    CallbackContext secondFailure(CallbackMode::FAILED);
    CallbackContext thirdFailure(CallbackMode::FAILED);
    System::StartupParticipant participants[3] = {
        participant("first", firstFailure, System::StartupPhase::BOOT,
            System::StartupRequirement::OPTIONAL),
        participant("second", secondFailure, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::OPTIONAL),
        participant("third", thirdFailure, System::StartupPhase::DOMAIN_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::StartupFailureRecord records[2];
    System::StartupFailureStorage storage { records, 2U };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 3U), storage
    );
    const System::StartupReport& report = runtime.start();
    TEST_ASSERT_EQUAL_UINT(3U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(2U, report.storedOptionalFailureCount());
    TEST_ASSERT_TRUE(report.optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_STRING("first", records[0].participantId());
    TEST_ASSERT_EQUAL_STRING("second", records[1].participantId());
}

void testOptionalFailuresRemainWhenLaterParticipantIsFatal() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext optional(CallbackMode::FAILED);
    CallbackContext required(CallbackMode::FAILED);
    System::StartupParticipant participants[2] = {
        participant("optional", optional, System::StartupPhase::BOOT,
            System::StartupRequirement::OPTIONAL),
        participant("required", required, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::StartupFailureRecord records[1];
    System::StartupFailureStorage storage { records, 1U };
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 2U), storage
    );
    const System::StartupReport* first = &runtime.start();
    TEST_ASSERT_TRUE(first->hasFatalFailure());
    TEST_ASSERT_EQUAL_UINT(1U, first->optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(1U, first->storedOptionalFailureCount());
    TEST_ASSERT_FALSE(first->optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_UINT(2U, first->totalFailureCount());
    TEST_ASSERT_EQUAL_STRING("optional", records[0].participantId());
    assertStatus(first->finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::CORE_INIT);
    const System::RuntimeStatus finalSnapshot = first->finalStatus();
    const int optionalCalls = optional.calls;
    const int requiredCalls = required.calls;

    const System::StartupReport* second = &runtime.start();
    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_INT(optionalCalls, optional.calls);
    TEST_ASSERT_EQUAL_INT(requiredCalls, required.calls);
    TEST_ASSERT_EQUAL_UINT(1U, second->optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(1U, second->storedOptionalFailureCount());
    TEST_ASSERT_FALSE(second->optionalFailuresTruncated());
    TEST_ASSERT_TRUE(second->hasFatalFailure());
    TEST_ASSERT_EQUAL_STRING("optional", records[0].participantId());
    const System::RuntimeStatus secondSnapshot = second->finalStatus();
    TEST_ASSERT_EQUAL_MEMORY(
        &finalSnapshot, &secondSnapshot, sizeof(finalSnapshot)
    );
}

void testOptionalOverflowRemainsTruncatedAfterLaterFatal() {
    CallbackContext early;
    CallbackContext gate;
    CallbackContext firstOptional(CallbackMode::FAILED);
    CallbackContext secondOptional(CallbackMode::FAILED);
    CallbackContext thirdOptional(CallbackMode::FAILED);
    CallbackContext required(CallbackMode::FAILED);
    System::StartupParticipant participants[4] = {
        participant("first", firstOptional, System::StartupPhase::BOOT,
            System::StartupRequirement::OPTIONAL),
        participant("second", secondOptional, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::OPTIONAL),
        participant("third", thirdOptional, System::StartupPhase::CORE_INIT,
            System::StartupRequirement::OPTIONAL),
        participant("required", required, System::StartupPhase::HARDWARE_INIT,
            System::StartupRequirement::REQUIRED)
    };
    System::StartupFailureRecord records[2];
    System::ApplicationRuntime runtime(
        plan(early, gate, participants, 4U),
        System::StartupFailureStorage { records, 2U });
    const System::StartupReport& report = runtime.start();
    assertStatus(report.finalStatus(), System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED,
        System::StartupPhase::HARDWARE_INIT);
    TEST_ASSERT_EQUAL_UINT(3U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(2U, report.storedOptionalFailureCount());
    TEST_ASSERT_TRUE(report.optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_UINT(4U, report.totalFailureCount());
    TEST_ASSERT_EQUAL_STRING("first", records[0].participantId());
    TEST_ASSERT_EQUAL_STRING("second", records[1].participantId());
    TEST_ASSERT_EQUAL_INT(1, required.calls);
}

} // namespace

void runApplicationRuntimeTests() {
    RUN_TEST(testCoreStartupErrorCatalogIsStable);
    RUN_TEST(testRuntimeReportIsIncompleteBeforeStart);
    RUN_TEST(testRuntimeSuccessAndSecondStartAreOneShot);
    RUN_TEST(testPlanRejectsInvalidParticipantPointerCountRelations);
    RUN_TEST(testPlanRejectsInvalidReportStorageRelationsAfterEarlySafe);
    RUN_TEST(testPlanRejectsMissingEarlyCallbackAndIdBeforeCallingAnything);
    RUN_TEST(testPlanRejectsMissingSafetyCallbackAfterEarlySafe);
    RUN_TEST(testPlanRejectsMissingSafetyGateIdAfterEarlySafe);
    RUN_TEST(testPlanRejectsDuplicateIdsAcrossAllActionKinds);
    RUN_TEST(testPlanRejectsMissingOrdinaryCallbackAndId);
    RUN_TEST(testPlanRejectsInvalidAndForbiddenPhases);
    RUN_TEST(testPlanRejectsInvalidRequirement);
    RUN_TEST(testPlanRejectsRequiredNetworkAndInterfacesParticipants);
    RUN_TEST(testPlanRejectsDecreasingPhaseOrder);
    RUN_TEST(testRuntimeExecutesFixedPhasesAndDeclarationOrder);
    RUN_TEST(testEarlyParticipantFailureIsFatalAndPreservesCode);
    RUN_TEST(testRequiredParticipantFailureIsFatalAndKeepsPhase);
    RUN_TEST(testSafetyGateFailureIsFatalAndSafetyRemainsLocked);
    RUN_TEST(testLateInvalidResultReturnsClearToLockedAndStopsCallbacks);
    RUN_TEST(testOrdinaryFailureRecordRetainsNonzeroIndexMetadata);
    RUN_TEST(testRequiredDisabledAndSpecialDisabledAreContractFailures);
    RUN_TEST(testOptionalDisabledDoesNotDegradeHealth);
    RUN_TEST(testInvalidCallbackResultsAreFatalContractFailures);
    RUN_TEST(testOptionalFailuresFillExactCapacityWithoutTruncation);
    RUN_TEST(testOptionalFailureWithZeroCapacityIsCountedAndTruncated);
    RUN_TEST(testOptionalFailureOverflowPreservesFirstN);
    RUN_TEST(testOptionalFailuresRemainWhenLaterParticipantIsFatal);
    RUN_TEST(testOptionalOverflowRemainsTruncatedAfterLaterFatal);
}
