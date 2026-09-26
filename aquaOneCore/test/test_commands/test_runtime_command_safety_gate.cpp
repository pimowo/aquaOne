#include <unity.h>

#include <AquaCore/Commands/CommandPipeline.h>
#include <AquaCore/Commands/RuntimeCommandSafetyGate.h>
#include <AquaCore/System/ApplicationRuntime.h>
#include <AquaCore/System/RuntimeStateCoordinator.h>

namespace {

namespace Commands = AquaCore::Commands;
namespace Safety = AquaCore::Safety;
namespace System = AquaCore::System;

enum class TestAction { Fill, Drain };
struct TestCommand { uint8_t actionCode; };

using Gate = Commands::RuntimeCommandSafetyGate<TestCommand, TestAction>;

struct Trace {
    char calls[64] {};
    size_t count = 0U;
    size_t handled = 0U;
    bool validatorAllows = true;
    bool policyAllows = true;

    void add(char marker) {
        calls[count++] = marker;
    }

    void clear() {
        count = 0U;
    }
};

struct StatusSource {
    System::RuntimeStatus current {};
    Trace* trace = nullptr;
    mutable size_t reads = 0U;
    bool available = true;

    StatusSource() {
        current.operational = System::OperationalState::RUNNING;
        current.health = System::HealthState::OK;
        current.safety = System::SafetyState::CLEAR;
        current.startupPhase = System::StartupPhase::RUNNING;
    }
};

bool readStatus(const void* context, System::RuntimeStatus& output) {
    if (context == nullptr) {
        return false;
    }
    const StatusSource& source = *static_cast<const StatusSource*>(context);
    ++source.reads;
    if (source.trace != nullptr) {
        source.trace->add('S');
    }
    if (!source.available) {
        return false;
    }
    output = source.current;
    return true;
}

struct ResolverContext {
    mutable size_t calls = 0U;
    bool available = true;
};

bool resolveAction(
    const TestCommand& command,
    const void* context,
    TestAction& output
) {
    if (context == nullptr) {
        return false;
    }
    const ResolverContext& resolver = *static_cast<const ResolverContext*>(context);
    ++resolver.calls;
    if (!resolver.available) {
        return false;
    }
    if (command.actionCode == 0U) {
        output = TestAction::Fill;
        return true;
    }
    if (command.actionCode == 1U) {
        output = TestAction::Drain;
        return true;
    }
    return false;
}

class MutableLockProvider final : public Safety::ActionLockProvider<TestAction> {
public:
    Safety::ActionLockContribution queryActionLock(const TestAction& action) const override {
        ++queries;
        if (action == TestAction::Fill) {
            return Safety::ActionLockContribution(fillLocked, fillCritical);
        }
        return Safety::ActionLockContribution(drainLocked, drainCritical);
    }

    bool fillLocked = false;
    bool fillCritical = false;
    bool drainLocked = false;
    bool drainCritical = false;
    mutable size_t queries = 0U;
};

bool validate(const TestCommand&, void* context) {
    Trace& trace = *static_cast<Trace*>(context);
    trace.add('V');
    return trace.validatorAllows;
}

bool policy(const TestCommand&, void* context) {
    Trace& trace = *static_cast<Trace*>(context);
    trace.add('P');
    return trace.policyAllows;
}

Commands::DomainCommandResult handle(const TestCommand&, void* context) {
    Trace& trace = *static_cast<Trace*>(context);
    trace.add('H');
    ++trace.handled;
    return Commands::DomainCommandResult::Completed;
}

struct Fixture {
    Trace trace {};
    StatusSource status {};
    ResolverContext resolver {};
    MutableLockProvider first {};
    MutableLockProvider second {};
    const Safety::ActionLockProvider<TestAction>* providers[2];
    Safety::ActionLockCoordinator<TestAction> locks;
    Gate gate;

    Fixture() : providers{&first, &second},
        locks(providers, 2U),
        gate(readStatus, &status, resolveAction, &resolver, &locks) {
        status.trace = &trace;
    }

    Commands::CommandPipelineConfig<TestCommand> config() {
        Commands::CommandPipelineConfig<TestCommand> value {};
        value.validator = validate;
        value.validatorContext = &trace;
        value.policy = policy;
        value.policyContext = &trace;
        value.safety = Gate::callback;
        value.safetyContext = &gate;
        value.handler = handle;
        value.handlerContext = &trace;
        return value;
    }
};

void assertOutcome(
    const Commands::CommandExecutionResult& result,
    Commands::CommandExecutionOutcome expected
) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(expected),
        static_cast<int>(result.outcome())
    );
    if (expected == Commands::CommandExecutionOutcome::Handled) {
        TEST_ASSERT_TRUE(result.hasDomainResult());
    } else {
        TEST_ASSERT_FALSE(result.hasDomainResult());
        TEST_ASSERT_NULL(result.domainResult());
    }
}

void test_running_clear_unlocked_reaches_handler() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.trace.handled);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.status.reads);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.resolver.calls);
    TEST_ASSERT_EQUAL_MEMORY("VPSH", fixture.trace.calls, 4U);
}

void test_non_running_states_block_normal_domain_commands() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    const System::OperationalState states[] = {
        System::OperationalState::BOOTING,
        System::OperationalState::ERROR,
        System::OperationalState::MAINTENANCE
    };
    for (size_t i = 0U; i < 3U; ++i) {
        fixture.status.current.operational = states[i];
        assertOutcome(pipeline.execute(TestCommand{0U}),
            Commands::CommandExecutionOutcome::BlockedBySafety);
    }
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.resolver.calls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.first.queries);
}

void test_global_safety_lock_blocks_unlocked_action() {
    Fixture fixture;
    fixture.status.current.safety = System::SafetyState::LOCKED;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.first.queries);
}

void test_degraded_and_fault_health_alone_do_not_block() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    fixture.status.current.health = System::HealthState::DEGRADED;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    fixture.status.current.health = System::HealthState::FAULT;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    TEST_ASSERT_EQUAL_UINT(2U, fixture.trace.handled);
}

void test_action_lock_is_granular_and_recovery_is_live() {
    Fixture fixture;
    fixture.first.fillLocked = true;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    assertOutcome(pipeline.execute(TestCommand{1U}),
        Commands::CommandExecutionOutcome::Handled);
    fixture.second.fillLocked = true;
    fixture.first.fillLocked = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    fixture.second.fillLocked = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    TEST_ASSERT_EQUAL_UINT(2U, fixture.trace.handled);
}

void test_ordinary_and_critical_action_locks_share_outcome() {
    Fixture fixture;
    fixture.first.fillLocked = true;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    fixture.first.fillCritical = true;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);
}

void test_invalid_provider_and_contribution_block() {
    Fixture fixture;
    fixture.first.fillCritical = true; // Invalid without locked.
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    TEST_ASSERT_FALSE(fixture.locks.query(TestAction::Fill).compositionValid);
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);

    const Safety::ActionLockProvider<TestAction>* invalidProviders[] = {
        &fixture.first, nullptr
    };
    Safety::ActionLockCoordinator<TestAction> invalidLocks(invalidProviders, 2U);
    Gate invalidGate(readStatus, &fixture.status, resolveAction, &fixture.resolver, &invalidLocks);
    Commands::CommandPipelineConfig<TestCommand> config = fixture.config();
    config.safetyContext = &invalidGate;
    Commands::CommandPipeline<TestCommand> invalidPipeline(config);
    assertOutcome(invalidPipeline.execute(TestCommand{1U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);
}

void test_missing_gate_dependencies_and_failed_reads_block() {
    Fixture fixture;
    Commands::CommandPipelineConfig<TestCommand> config = fixture.config();
    Gate noStatus(nullptr, nullptr, resolveAction, &fixture.resolver, &fixture.locks);
    Gate noResolver(readStatus, &fixture.status, nullptr, nullptr, &fixture.locks);
    Gate noLocks(readStatus, &fixture.status, resolveAction, &fixture.resolver, nullptr);
    Gate* invalidGates[] = {&noStatus, &noResolver, &noLocks};
    for (size_t i = 0U; i < 3U; ++i) {
        config.safetyContext = invalidGates[i];
        Commands::CommandPipeline<TestCommand> pipeline(config);
        assertOutcome(pipeline.execute(TestCommand{0U}),
            Commands::CommandExecutionOutcome::BlockedBySafety);
    }
    config.safetyContext = nullptr;
    Commands::CommandPipeline<TestCommand> noGate(config);
    assertOutcome(noGate.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);

    config.safetyContext = &fixture.gate;
    Commands::CommandPipeline<TestCommand> pipeline(config);
    fixture.status.available = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    fixture.status.available = true;
    fixture.resolver.available = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    fixture.resolver.available = true;
    assertOutcome(pipeline.execute(TestCommand{255U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);
}

void test_real_gate_preserves_pipeline_order_and_short_circuit() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> pipeline(fixture.config());
    fixture.trace.validatorAllows = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::InvalidCommand);
    TEST_ASSERT_EQUAL_MEMORY("V", fixture.trace.calls, 1U);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.status.reads);

    fixture.trace.clear();
    fixture.trace.validatorAllows = true;
    fixture.trace.policyAllows = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedByPolicy);
    TEST_ASSERT_EQUAL_MEMORY("VP", fixture.trace.calls, 2U);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.status.reads);

    fixture.trace.clear();
    fixture.trace.policyAllows = true;
    fixture.first.fillLocked = true;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    TEST_ASSERT_EQUAL_MEMORY("VPS", fixture.trace.calls, 3U);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.trace.handled);

    fixture.trace.clear();
    fixture.first.fillLocked = false;
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    TEST_ASSERT_EQUAL_MEMORY("VPSH", fixture.trace.calls, 4U);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.trace.handled);
}

System::StartupStepResult startupSuccess(void*) {
    return System::StartupStepResult::succeeded();
}

bool readApplicationRuntime(const void* context, System::RuntimeStatus& output) {
    if (context == nullptr) {
        return false;
    }
    output = static_cast<const System::ApplicationRuntime*>(context)->status();
    return true;
}

class MutableSafetyProvider final : public System::SafetyProvider {
public:
    System::SafetyState safetyContribution() const override {
        return state;
    }
    System::SafetyState state = System::SafetyState::CLEAR;
};

void test_live_runtime_status_after_handoff_and_refresh() {
    System::ApplicationPlan plan {};
    plan.earlySafeOutputs = System::StartupAction{"early", startupSuccess, nullptr};
    plan.safetyGate = System::StartupAction{"safety", startupSuccess, nullptr};
    System::ApplicationRuntime runtime(
        plan, System::StartupFailureStorage{nullptr, 0U}
    );
    MutableSafetyProvider safetyProvider;
    const System::SafetyProvider* safetyProviders[] = {&safetyProvider};
    System::RuntimeStateCoordinator coordinator(
        nullptr, 0U, safetyProviders, 1U
    );
    TEST_ASSERT_FALSE(runtime.start().hasFatalFailure());
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));

    Fixture fixture;
    Gate liveGate(
        readApplicationRuntime, &runtime,
        resolveAction, &fixture.resolver, &fixture.locks
    );
    Commands::CommandPipelineConfig<TestCommand> config = fixture.config();
    config.safetyContext = &liveGate;
    Commands::CommandPipeline<TestCommand> pipeline(config);

    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    safetyProvider.state = System::SafetyState::LOCKED;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
        static_cast<int>(coordinator.refresh())
    );
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::BlockedBySafety);
    safetyProvider.state = System::SafetyState::CLEAR;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
        static_cast<int>(coordinator.refresh())
    );
    assertOutcome(pipeline.execute(TestCommand{0U}),
        Commands::CommandExecutionOutcome::Handled);
    TEST_ASSERT_EQUAL_UINT(2U, fixture.trace.handled);
}

} // namespace

void runRuntimeCommandSafetyGateTests() {
    RUN_TEST(test_running_clear_unlocked_reaches_handler);
    RUN_TEST(test_non_running_states_block_normal_domain_commands);
    RUN_TEST(test_global_safety_lock_blocks_unlocked_action);
    RUN_TEST(test_degraded_and_fault_health_alone_do_not_block);
    RUN_TEST(test_action_lock_is_granular_and_recovery_is_live);
    RUN_TEST(test_ordinary_and_critical_action_locks_share_outcome);
    RUN_TEST(test_invalid_provider_and_contribution_block);
    RUN_TEST(test_missing_gate_dependencies_and_failed_reads_block);
    RUN_TEST(test_real_gate_preserves_pipeline_order_and_short_circuit);
    RUN_TEST(test_live_runtime_status_after_handoff_and_refresh);
}
