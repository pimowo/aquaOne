#include <unity.h>

#include <AquaCore/Commands/CommandPipeline.h>
#include <AquaCore/Commands/RuntimeCommandPolicyGate.h>
#include <AquaCore/Commands/RuntimeCommandSafetyGate.h>

namespace {

namespace Commands = AquaCore::Commands;
namespace Safety = AquaCore::Safety;
namespace System = AquaCore::System;

struct TestCommand {
    Commands::CommandClass commandClass;
    uint8_t action;
};
enum class TestAction : uint8_t { Run };

struct Fixture {
    System::RuntimeStatus status {
        System::OperationalState::RUNNING,
        System::HealthState::OK,
        System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING
    };
    mutable unsigned reads = 0U;
    mutable unsigned resolutions = 0U;
    unsigned handled = 0U;
    bool statusAvailable = true;
    bool classAvailable = true;
    bool actionLocked = false;

    class LockProvider final : public Safety::ActionLockProvider<TestAction> {
    public:
        explicit LockProvider(const Fixture& owner) : owner_(owner) {}
        Safety::ActionLockContribution queryActionLock(const TestAction&) const override {
            return Safety::ActionLockContribution(owner_.actionLocked, false);
        }
    private:
        const Fixture& owner_;
    } provider;

    const Safety::ActionLockProvider<TestAction>* providers[1];
    Safety::ActionLockCoordinator<TestAction> locks;
    Commands::RuntimeCommandPolicyGate<TestCommand> policy;
    Commands::RuntimeCommandPolicyGate<TestCommand> recoveryPolicy;
    Commands::RuntimeCommandSafetyGate<TestCommand, TestAction> safety;

    Fixture() : provider(*this), providers{&provider}, locks(providers, 1U),
        policy(readStatus, this, resolveClass, this,
            Commands::CommandClass::NormalDomain),
        recoveryPolicy(readStatus, this, resolveClass, this,
            Commands::CommandClass::SystemRecovery),
        safety(readStatus, this, resolveAction, this, &locks) {}

    static bool readStatus(const void* context, System::RuntimeStatus& out) {
        const Fixture& fixture = *static_cast<const Fixture*>(context);
        ++fixture.reads;
        if (!fixture.statusAvailable) return false;
        out = fixture.status;
        return true;
    }

    static bool resolveClass(const TestCommand& command, const void* context,
                             Commands::CommandClass& out) {
        const Fixture& fixture = *static_cast<const Fixture*>(context);
        ++fixture.resolutions;
        if (!fixture.classAvailable) return false;
        out = command.commandClass;
        return true;
    }

    static bool resolveAction(const TestCommand& command, const void*, TestAction& out) {
        if (command.action != 0U) return false;
        out = TestAction::Run;
        return true;
    }

    static bool validate(const TestCommand&, void*) { return true; }

    static Commands::DomainCommandResult handle(const TestCommand&, void* context) {
        ++static_cast<Fixture*>(context)->handled;
        return Commands::DomainCommandResult::Completed;
    }

    Commands::CommandPipelineConfig<TestCommand> normalPipelineConfig() {
        Commands::CommandPipelineConfig<TestCommand> config {};
        config.validator = validate;
        config.policy = Commands::RuntimeCommandPolicyGate<TestCommand>::callback;
        config.policyContext = &policy;
        config.safety = Commands::RuntimeCommandSafetyGate<TestCommand, TestAction>::callback;
        config.safetyContext = &safety;
        config.handler = handle;
        config.handlerContext = this;
        return config;
    }
};

const TestCommand normal {Commands::CommandClass::NormalDomain, 0U};
const TestCommand recovery {Commands::CommandClass::SystemRecovery, 0U};

void test_normal_domain_operational_policy() {
    Fixture fixture;
    const System::OperationalState states[] = {
        System::OperationalState::RUNNING, System::OperationalState::BOOTING,
        System::OperationalState::ERROR, System::OperationalState::MAINTENANCE
    };
    for (unsigned i = 0U; i < 4U; ++i) {
        fixture.status.operational = states[i];
        TEST_ASSERT_EQUAL(i == 0U, fixture.policy.allows(normal));
        TEST_ASSERT_FALSE(fixture.policy.allows(recovery));
    }
    TEST_ASSERT_EQUAL_UINT(8U, fixture.reads);
}

void test_system_recovery_operational_policy() {
    Fixture fixture;
    const System::OperationalState states[] = {
        System::OperationalState::RUNNING, System::OperationalState::BOOTING,
        System::OperationalState::ERROR, System::OperationalState::MAINTENANCE
    };
    for (unsigned i = 0U; i < 4U; ++i) {
        fixture.status.operational = states[i];
        TEST_ASSERT_EQUAL(i == 0U || i == 2U, fixture.recoveryPolicy.allows(recovery));
        TEST_ASSERT_FALSE(fixture.recoveryPolicy.allows(normal));
    }
}

void test_error_shell_policy_does_not_execute_domain_handler() {
    Fixture fixture;
    fixture.status.operational = System::OperationalState::ERROR;
    Commands::CommandPipeline<TestCommand> domain(fixture.normalPipelineConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(domain.execute(normal).outcome()));
    TEST_ASSERT_TRUE(fixture.recoveryPolicy.allows(recovery));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(domain.execute(recovery).outcome()));
    fixture.status.operational = System::OperationalState::RUNNING;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(domain.execute(recovery).outcome()));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.handled);
    // Recovery is only a policy decision here, never passed to Domain handler.
}

void test_policy_and_safety_outcomes_are_distinct() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> domain(fixture.normalPipelineConfig());
    fixture.status.operational = System::OperationalState::ERROR;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(domain.execute(normal).outcome()));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.reads);
    fixture.status.operational = System::OperationalState::RUNNING;
    fixture.status.safety = System::SafetyState::LOCKED;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedBySafety),
        static_cast<int>(domain.execute(normal).outcome()));
    fixture.status.safety = System::SafetyState::CLEAR;
    fixture.actionLocked = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedBySafety),
        static_cast<int>(domain.execute(normal).outcome()));
    fixture.actionLocked = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::Handled),
        static_cast<int>(domain.execute(normal).outcome()));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.handled);
}

void test_policy_fails_closed_for_invalid_composition() {
    Fixture fixture;
    using Gate = Commands::RuntimeCommandPolicyGate<TestCommand>;
    Gate noReader(nullptr, nullptr, Fixture::resolveClass, &fixture,
                  Commands::CommandClass::NormalDomain);
    Gate noResolver(Fixture::readStatus, &fixture, nullptr, nullptr,
                    Commands::CommandClass::NormalDomain);
    Gate invalidRoute(Fixture::readStatus, &fixture, Fixture::resolveClass,
                      &fixture, static_cast<Commands::CommandClass>(255U));
    TEST_ASSERT_FALSE(noReader.allows(normal));
    TEST_ASSERT_FALSE(noResolver.allows(normal));
    TEST_ASSERT_FALSE(invalidRoute.allows(normal));
    TEST_ASSERT_FALSE(Gate::callback(normal, nullptr));
    fixture.statusAvailable = false;
    TEST_ASSERT_FALSE(fixture.policy.allows(normal));
    fixture.statusAvailable = true;
    fixture.classAvailable = false;
    TEST_ASSERT_FALSE(fixture.policy.allows(normal));
    fixture.classAvailable = true;
    const TestCommand invalid {static_cast<Commands::CommandClass>(255U), 0U};
    TEST_ASSERT_FALSE(fixture.policy.allows(invalid));
    fixture.status.operational = static_cast<System::OperationalState>(255U);
    TEST_ASSERT_FALSE(fixture.policy.allows(normal));
    TEST_ASSERT_FALSE(fixture.recoveryPolicy.allows(recovery));
}

void test_live_status_is_read_on_every_policy_call() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.policy.allows(normal));
    fixture.status.operational = System::OperationalState::ERROR;
    TEST_ASSERT_FALSE(fixture.policy.allows(normal));
    TEST_ASSERT_TRUE(fixture.recoveryPolicy.allows(recovery));
    // Fake source changes again solely to prove fresh reads; production ERROR
    // recovery requires restart and cannot transition to RUNNING in place.
    fixture.status.operational = System::OperationalState::RUNNING;
    TEST_ASSERT_TRUE(fixture.policy.allows(normal));
    TEST_ASSERT_EQUAL_UINT(4U, fixture.reads);
}

void test_health_alone_does_not_block_policy_or_safety() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> domain(fixture.normalPipelineConfig());
    fixture.status.health = System::HealthState::DEGRADED;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::Handled),
        static_cast<int>(domain.execute(normal).outcome()));
    fixture.status.health = System::HealthState::FAULT;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::Handled),
        static_cast<int>(domain.execute(normal).outcome()));
    TEST_ASSERT_EQUAL_UINT(2U, fixture.handled);
}

} // namespace

void runRuntimeCommandPolicyGateTests() {
    RUN_TEST(test_normal_domain_operational_policy);
    RUN_TEST(test_system_recovery_operational_policy);
    RUN_TEST(test_error_shell_policy_does_not_execute_domain_handler);
    RUN_TEST(test_policy_and_safety_outcomes_are_distinct);
    RUN_TEST(test_policy_fails_closed_for_invalid_composition);
    RUN_TEST(test_live_status_is_read_on_every_policy_call);
    RUN_TEST(test_health_alone_does_not_block_policy_or_safety);
}