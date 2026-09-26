#include <unity.h>

#include <AquaCore/System/ApplicationRuntime.h>
#include <AquaCore/System/RuntimeStateCoordinator.h>

namespace {

namespace System = AquaCore::System;

const char ID_EARLY[] = {'e','a','r','l','y','\0'};
const char ID_GATE[] = {'g','a','t','e','\0'};
const char ID_OPTIONAL[] = {'o','p','t','i','o','n','a','l','\0'};
const char ID_SECOND[] = {'s','e','c','o','n','d','\0'};
const char ID_THIRD[] = {'t','h','i','r','d','\0'};
const char ID_REQUIRED[] = {'r','e','q','u','i','r','e','d','\0'};
const char ID_NETWORK[] = {'n','e','t','w','o','r','k','\0'};
const char ERROR_OWNER[] = {'A','Q','U','A','.','T','E','S','T','\0'};
const char ERROR_OWNER_INVALID[] = {'A','q','u','a','.','T','E','S','T','\0'};
const char ERROR_CODE[] = {'F','A','I','L','E','D','\0'};

class FakeHealthProvider final : public System::HealthProvider {
public:
    System::HealthState healthContribution() const override {
        ++queries;
        return state;
    }

    System::HealthState state = System::HealthState::OK;
    mutable size_t queries = 0U;
};

class FakeSafetyProvider final : public System::SafetyProvider {
public:
    System::SafetyState safetyContribution() const override {
        ++queries;
        return state;
    }

    System::SafetyState state = System::SafetyState::CLEAR;
    mutable size_t queries = 0U;
};

class InvalidHealthProvider final : public System::HealthProvider {
public:
    System::HealthState healthContribution() const override {
        return static_cast<System::HealthState>(0xFFU);
    }
};

class InvalidSafetyProvider final : public System::SafetyProvider {
public:
    System::SafetyState safetyContribution() const override {
        return static_cast<System::SafetyState>(0xFFU);
    }
};

enum class StartupMode {
    SUCCESS,
    FAILED,
    INVALID
};

struct StartupContext {
    StartupMode mode = StartupMode::SUCCESS;
    FakeHealthProvider* health = nullptr;
    size_t calls = 0U;
};

System::StartupStepResult startupCallback(void* rawContext) {
    StartupContext& context = *static_cast<StartupContext*>(rawContext);
    ++context.calls;
    if (context.mode == StartupMode::SUCCESS) {
        return System::StartupStepResult::succeeded();
    }
    if (context.health != nullptr) {
        context.health->state = System::HealthState::DEGRADED;
    }
    if (context.mode == StartupMode::INVALID) {
        return System::StartupStepResult::failed(
            System::StartupErrorCode::fromStatic(
                ERROR_OWNER_INVALID,
                ERROR_CODE
            )
        );
    }
    return System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic(ERROR_OWNER, ERROR_CODE)
    );
}

System::StartupAction startupAction(
    const char* id,
    StartupContext& context
) {
    System::StartupAction value {};
    value.participantId = id;
    value.callback = startupCallback;
    value.context = &context;
    return value;
}

System::StartupParticipant startupParticipant(
    const char* id,
    StartupContext& context,
    System::StartupPhase phase,
    System::StartupRequirement requirement
) {
    System::StartupParticipant value {};
    value.action = startupAction(id, context);
    value.phase = phase;
    value.requirement = requirement;
    return value;
}

System::ApplicationPlan startupPlan(
    StartupContext& early,
    StartupContext& gate,
    const System::StartupParticipant* participants,
    size_t participantCount
) {
    System::ApplicationPlan value {};
    value.earlySafeOutputs = startupAction(ID_EARLY, early);
    value.safetyGate = startupAction(ID_GATE, gate);
    value.participants = participants;
    value.participantCount = participantCount;
    return value;
}

System::StartupFailureStorage noFailureStorage() {
    return System::StartupFailureStorage {nullptr, 0U};
}

void assertLiveState(
    const System::ApplicationRuntime& runtime,
    System::OperationalState operational,
    System::HealthState health,
    System::SafetyState safety
) {
    const System::RuntimeStatus status = runtime.status();
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
}

void testCoordinatorRefreshBeforeHandoffIsInactiveAndDoesNotMutate() {
    System::RuntimeStateCoordinator coordinator(nullptr, 0U, nullptr, 0U);
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    const System::RuntimeStatus before = runtime.status();

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::INACTIVE),
        static_cast<int>(coordinator.refresh())
    );
    const System::RuntimeStatus after = runtime.status();
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_FALSE(coordinator.isActive());
}

void testZeroProvidersHandoffKeepsOkClearWithoutGlitch() {
    System::RuntimeStateCoordinator coordinator(nullptr, 0U, nullptr, 0U);
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_TRUE(runtime.runtimeStateHandedOff());
    TEST_ASSERT_TRUE(coordinator.isActive());
    TEST_ASSERT_TRUE(coordinator.isCompositionValid());
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
        static_cast<int>(coordinator.refresh())
    );
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
}

void testHealthAggregationDominanceAndRecovery() {
    FakeHealthProvider first;
    FakeHealthProvider second;
    const System::HealthProvider* providers[] = {&first, &second};
    System::RuntimeStateCoordinator coordinator(
        providers, 2U, nullptr, 0U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));

    first.state = System::HealthState::DEGRADED;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
    second.state = System::HealthState::FAULT;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::FAULT, System::SafetyState::CLEAR);
    second.state = System::HealthState::OK;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
    first.state = System::HealthState::OK;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
}

void testSafetyAggregationRequiresAllProvidersClear() {
    FakeSafetyProvider first;
    FakeSafetyProvider second;
    const System::SafetyProvider* providers[] = {&first, &second};
    System::RuntimeStateCoordinator coordinator(
        nullptr, 0U, providers, 2U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));

    first.state = System::SafetyState::LOCKED;
    second.state = System::SafetyState::LOCKED;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::LOCKED);
    first.state = System::SafetyState::CLEAR;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::LOCKED);
    second.state = System::SafetyState::CLEAR;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
}

void testSingleClearSafetyProviderKeepsClear() {
    FakeSafetyProvider provider;
    const System::SafetyProvider* providers[] = {&provider};
    System::RuntimeStateCoordinator coordinator(
        nullptr, 0U, providers, 1U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
        static_cast<int>(coordinator.refresh())
    );
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
}

void testCoordinatorInstancesAndContextsAreIndependent() {
    FakeHealthProvider firstProvider;
    FakeHealthProvider secondProvider;
    const System::HealthProvider* firstProviders[] = {&firstProvider};
    const System::HealthProvider* secondProviders[] = {&secondProvider};
    System::RuntimeStateCoordinator firstCoordinator(
        firstProviders, 1U, nullptr, 0U
    );
    System::RuntimeStateCoordinator secondCoordinator(
        secondProviders, 1U, nullptr, 0U
    );
    StartupContext firstEarly;
    StartupContext firstGate;
    StartupContext secondEarly;
    StartupContext secondGate;
    System::ApplicationRuntime firstRuntime(
        startupPlan(firstEarly, firstGate, nullptr, 0U),
        noFailureStorage()
    );
    System::ApplicationRuntime secondRuntime(
        startupPlan(secondEarly, secondGate, nullptr, 0U),
        noFailureStorage()
    );
    firstRuntime.start();
    secondRuntime.start();
    TEST_ASSERT_TRUE(firstRuntime.handoffRuntimeState(firstCoordinator));
    TEST_ASSERT_TRUE(secondRuntime.handoffRuntimeState(secondCoordinator));

    firstProvider.state = System::HealthState::FAULT;
    firstCoordinator.refresh();
    assertLiveState(firstRuntime, System::OperationalState::RUNNING,
        System::HealthState::FAULT, System::SafetyState::CLEAR);
    assertLiveState(secondRuntime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
    TEST_ASSERT_EQUAL_UINT(2U, firstProvider.queries);
    TEST_ASSERT_EQUAL_UINT(1U, secondProvider.queries);
}

void testNullProviderEntryRejectsHandoffAsCompositionFailure() {
    const System::SafetyProvider* providers[] = {nullptr};
    System::RuntimeStateCoordinator coordinator(
        nullptr, 0U, providers, 1U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_FALSE(coordinator.isCompositionValid());
    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(runtime.runtimeStateHandedOff());
    TEST_ASSERT_FALSE(coordinator.isActive());
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::INACTIVE),
        static_cast<int>(coordinator.refresh())
    );
}

void testInvalidProviderResultsRejectHandoffWithoutPartialActivation() {
    InvalidHealthProvider invalidHealth;
    InvalidSafetyProvider invalidSafety;
    const System::HealthProvider* health[] = {&invalidHealth};
    const System::SafetyProvider* safety[] = {&invalidSafety};
    System::RuntimeStateCoordinator coordinator(
        health, 1U, safety, 1U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(runtime.runtimeStateHandedOff());
    TEST_ASSERT_FALSE(coordinator.isActive());
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::INACTIVE),
        static_cast<int>(coordinator.refresh())
    );
}

void testInvalidSafetyContributionRejectsHandoff() {
    InvalidSafetyProvider invalidSafety;
    const System::SafetyProvider* safety[] = {&invalidSafety};
    System::RuntimeStateCoordinator coordinator(
        nullptr, 0U, safety, 1U
    );
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(runtime.runtimeStateHandedOff());
    TEST_ASSERT_FALSE(coordinator.isActive());
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
}

void testMissingStartupContributionIsExplicitCompositionFailure() {
    StartupContext early;
    StartupContext gate;
    StartupContext optional;
    optional.mode = StartupMode::FAILED;
    System::StartupParticipant participants[] = {
        startupParticipant(
            ID_OPTIONAL,
            optional,
            System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL
        )
    };
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, participants, 1U),
        noFailureStorage()
    );
    System::RuntimeStateCoordinator coordinator(nullptr, 0U, nullptr, 0U);
    const System::StartupReport& report = runtime.start();

    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(runtime.runtimeStateHandedOff());
    TEST_ASSERT_FALSE(coordinator.isActive());
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::INACTIVE),
        static_cast<int>(coordinator.refresh())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(report.finalStatus().health)
    );
    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
}

void testStartupDegradedWithZeroReportCapacitySurvivesHandoff() {
    FakeHealthProvider startupHealth;
    StartupContext early;
    StartupContext gate;
    StartupContext optional;
    optional.mode = StartupMode::FAILED;
    optional.health = &startupHealth;
    System::StartupParticipant participants[] = {
        startupParticipant(
            ID_OPTIONAL,
            optional,
            System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL
        )
    };
    const System::HealthProvider* providers[] = {&startupHealth};
    System::RuntimeStateCoordinator coordinator(
        providers, 1U, nullptr, 0U
    );
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, participants, 1U),
        noFailureStorage()
    );
    const System::StartupReport& report = runtime.start();

    TEST_ASSERT_TRUE(report.optionalFailuresTruncated());
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
    startupHealth.state = System::HealthState::OK;
    coordinator.refresh();
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::OK, System::SafetyState::CLEAR);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(report.finalStatus().health)
    );
}

void testStartupDegradedSurvivesReportOverflow() {
    FakeHealthProvider startupHealth;
    StartupContext early;
    StartupContext gate;
    StartupContext first;
    StartupContext second;
    StartupContext third;
    first.mode = StartupMode::FAILED;
    second.mode = StartupMode::FAILED;
    third.mode = StartupMode::FAILED;
    first.health = &startupHealth;
    second.health = &startupHealth;
    third.health = &startupHealth;
    System::StartupParticipant participants[] = {
        startupParticipant(ID_OPTIONAL, first,
            System::StartupPhase::BOOT,
            System::StartupRequirement::OPTIONAL),
        startupParticipant(ID_SECOND, second,
            System::StartupPhase::CORE_INIT,
            System::StartupRequirement::OPTIONAL),
        startupParticipant(ID_THIRD, third,
            System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL)
    };
    System::StartupFailureRecord record[1];
    const System::HealthProvider* providers[] = {&startupHealth};
    System::RuntimeStateCoordinator coordinator(
        providers, 1U, nullptr, 0U
    );
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, participants, 3U),
        System::StartupFailureStorage {record, 1U}
    );
    const System::StartupReport& report = runtime.start();

    TEST_ASSERT_TRUE(report.optionalFailuresTruncated());
    TEST_ASSERT_EQUAL_UINT(3U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_UINT(1U, report.storedOptionalFailureCount());
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::DEGRADED, System::SafetyState::CLEAR);
}

void testFatalStartupNeverHandsOffOrAllowsRefresh() {
    FakeHealthProvider health;
    health.state = System::HealthState::OK;
    const System::HealthProvider* providers[] = {&health};
    System::RuntimeStateCoordinator coordinator(
        providers, 1U, nullptr, 0U
    );
    StartupContext early;
    StartupContext gate;
    StartupContext required;
    required.mode = StartupMode::FAILED;
    System::StartupParticipant participants[] = {
        startupParticipant(
            ID_REQUIRED,
            required,
            System::StartupPhase::HARDWARE_INIT,
            System::StartupRequirement::REQUIRED
        )
    };
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, participants, 1U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(coordinator.isActive());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::INACTIVE),
        static_cast<int>(coordinator.refresh())
    );
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
}

void testLateFatalAfterSafetyClearNeverHandsOff() {
    System::RuntimeStateCoordinator coordinator(nullptr, 0U, nullptr, 0U);
    StartupContext early;
    StartupContext gate;
    StartupContext network;
    network.mode = StartupMode::INVALID;
    System::StartupParticipant participants[] = {
        startupParticipant(
            ID_NETWORK,
            network,
            System::StartupPhase::NETWORK_INIT,
            System::StartupRequirement::OPTIONAL
        )
    };
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, participants, 1U),
        noFailureStorage()
    );
    runtime.start();

    TEST_ASSERT_EQUAL_UINT(1U, gate.calls);
    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_FALSE(coordinator.isActive());
    assertLiveState(runtime, System::OperationalState::ERROR,
        System::HealthState::FAULT, System::SafetyState::LOCKED);
}

void testSecondHandoffIsRejectedAndDoesNotResetState() {
    FakeHealthProvider health;
    const System::HealthProvider* providers[] = {&health};
    System::RuntimeStateCoordinator first(providers, 1U, nullptr, 0U);
    System::RuntimeStateCoordinator second(nullptr, 0U, nullptr, 0U);
    StartupContext early;
    StartupContext gate;
    System::ApplicationRuntime runtime(
        startupPlan(early, gate, nullptr, 0U),
        noFailureStorage()
    );
    runtime.start();
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(first));
    health.state = System::HealthState::FAULT;
    first.refresh();
    runtime.start();

    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(first));
    TEST_ASSERT_FALSE(runtime.handoffRuntimeState(second));
    TEST_ASSERT_TRUE(first.isActive());
    TEST_ASSERT_FALSE(second.isActive());
    assertLiveState(runtime, System::OperationalState::RUNNING,
        System::HealthState::FAULT, System::SafetyState::CLEAR);
}

} // namespace

void runRuntimeStateCoordinatorTests() {
    RUN_TEST(testCoordinatorRefreshBeforeHandoffIsInactiveAndDoesNotMutate);
    RUN_TEST(testZeroProvidersHandoffKeepsOkClearWithoutGlitch);
    RUN_TEST(testHealthAggregationDominanceAndRecovery);
    RUN_TEST(testSafetyAggregationRequiresAllProvidersClear);
    RUN_TEST(testSingleClearSafetyProviderKeepsClear);
    RUN_TEST(testCoordinatorInstancesAndContextsAreIndependent);
    RUN_TEST(testNullProviderEntryRejectsHandoffAsCompositionFailure);
    RUN_TEST(testInvalidProviderResultsRejectHandoffWithoutPartialActivation);
    RUN_TEST(testInvalidSafetyContributionRejectsHandoff);
    RUN_TEST(testMissingStartupContributionIsExplicitCompositionFailure);
    RUN_TEST(testStartupDegradedWithZeroReportCapacitySurvivesHandoff);
    RUN_TEST(testStartupDegradedSurvivesReportOverflow);
    RUN_TEST(testFatalStartupNeverHandsOffOrAllowsRefresh);
    RUN_TEST(testLateFatalAfterSafetyClearNeverHandsOff);
    RUN_TEST(testSecondHandoffIsRejectedAndDoesNotResetState);
}
