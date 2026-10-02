#include <unity.h>

#include <AquaCore/Commands/CommandPipeline.h>
#include <AquaCore/Commands/RuntimeMaintenanceCommandPolicyGate.h>
#include <AquaCore/Commands/RuntimeCommandPolicyGate.h>

namespace {

namespace Commands = AquaCore::Commands;
namespace System = AquaCore::System;

struct TestCommand {
    Commands::CommandClass commandClass;
    Commands::MaintenanceCommandKind kind;
};

struct Fixture {
    using MaintenanceGate =
        Commands::RuntimeMaintenanceCommandPolicyGate<TestCommand>;
    using PolicyGate = Commands::RuntimeCommandPolicyGate<TestCommand>;

    System::RuntimeStatus status {
        System::OperationalState::RUNNING,
        System::HealthState::OK,
        System::SafetyState::CLEAR,
        System::StartupPhase::RUNNING
    };
    mutable unsigned statusReads = 0U;
    mutable unsigned classReads = 0U;
    mutable unsigned kindReads = 0U;
    unsigned safetyCalls = 0U;
    unsigned handlerCalls = 0U;
    bool statusAvailable = true;
    bool classAvailable = true;
    bool kindAvailable = true;

    MaintenanceGate maintenancePolicy;
    PolicyGate normalPolicy;
    PolicyGate recoveryPolicy;

    Fixture() : maintenancePolicy(
            readStatus, this, resolveClass, this, resolveKind, this),
        normalPolicy(readStatus, this, resolveClass, this,
                     Commands::CommandClass::NormalDomain),
        recoveryPolicy(readStatus, this, resolveClass, this,
                       Commands::CommandClass::SystemRecovery) {
    }

    static bool readStatus(const void* context, System::RuntimeStatus& out) {
        const Fixture& fixture = *static_cast<const Fixture*>(context);
        ++fixture.statusReads;
        if (!fixture.statusAvailable) return false;
        out = fixture.status;
        return true;
    }

    static bool resolveClass(const TestCommand& command, const void* context,
                             Commands::CommandClass& out) {
        const Fixture& fixture = *static_cast<const Fixture*>(context);
        ++fixture.classReads;
        if (!fixture.classAvailable) return false;
        out = command.commandClass;
        return true;
    }

    static bool resolveKind(const TestCommand& command, const void* context,
                            Commands::MaintenanceCommandKind& out) {
        const Fixture& fixture = *static_cast<const Fixture*>(context);
        ++fixture.kindReads;
        if (!fixture.kindAvailable) return false;
        out = command.kind;
        return true;
    }

    static bool validate(const TestCommand&, void*) { return true; }

    static bool safety(const TestCommand&, void* context) {
        ++static_cast<Fixture*>(context)->safetyCalls;
        return true;
    }

    static Commands::DomainCommandResult handle(const TestCommand&, void* context) {
        ++static_cast<Fixture*>(context)->handlerCalls;
        return Commands::DomainCommandResult::Completed;
    }

    Commands::CommandPipelineConfig<TestCommand> pipelineConfig(
        Commands::CommandPipelineConfig<TestCommand>::Gate policy,
        void* policyContext
    ) {
        Commands::CommandPipelineConfig<TestCommand> config {};
        config.validator = validate;
        config.policy = policy;
        config.policyContext = policyContext;
        config.safety = safety;
        config.safetyContext = this;
        config.handler = handle;
        config.handlerContext = this;
        return config;
    }
};

TestCommand command(Commands::CommandClass commandClass,
                    Commands::MaintenanceCommandKind kind) {
    return TestCommand {commandClass, kind};
}

void testMaintenancePolicyMatrixAndIdempotencyDelegation() {
    Fixture fixture;
    const System::OperationalState states[] = {
        System::OperationalState::BOOTING,
        System::OperationalState::RUNNING,
        System::OperationalState::MAINTENANCE,
        System::OperationalState::ERROR
    };

    for (System::OperationalState state : states) {
        fixture.status.operational = state;
        const bool enterAllowed =
            state == System::OperationalState::RUNNING ||
            state == System::OperationalState::MAINTENANCE;
        const bool operationAllowed =
            state == System::OperationalState::MAINTENANCE;
        const bool exitAllowed =
            state == System::OperationalState::RUNNING ||
            state == System::OperationalState::MAINTENANCE;
        TEST_ASSERT_EQUAL(enterAllowed, fixture.maintenancePolicy.allows(
            command(Commands::CommandClass::Maintenance,
                    Commands::MaintenanceCommandKind::Enter)));
        TEST_ASSERT_EQUAL(operationAllowed, fixture.maintenancePolicy.allows(
            command(Commands::CommandClass::Maintenance,
                    Commands::MaintenanceCommandKind::Operation)));
        TEST_ASSERT_EQUAL(exitAllowed, fixture.maintenancePolicy.allows(
            command(Commands::CommandClass::Maintenance,
                    Commands::MaintenanceCommandKind::Exit)));
    }

    // Enter/Exit in the target state are delegated to F5.3's authoritative
    // AlreadyInTargetState result; Operation has no such exception.
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(command(
        Commands::CommandClass::Maintenance,
        static_cast<Commands::MaintenanceCommandKind>(0xFFU))));
    fixture.status.operational = static_cast<System::OperationalState>(0xFFU);
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Enter)));
}

void testMaintenancePolicyReadsLiveOperationalStatus() {
    Fixture fixture;
    const TestCommand operation = command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Operation);
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(operation));
    fixture.status.operational = System::OperationalState::MAINTENANCE;
    TEST_ASSERT_TRUE(fixture.maintenancePolicy.allows(operation));
    fixture.status.operational = System::OperationalState::ERROR;
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(operation));
    TEST_ASSERT_EQUAL_UINT(3U, fixture.statusReads);
    TEST_ASSERT_EQUAL_UINT(3U, fixture.classReads);
    TEST_ASSERT_EQUAL_UINT(3U, fixture.kindReads);
}

void testMaintenancePolicyFailsClosedForMissingAndInvalidInputs() {
    Fixture fixture;
    using Gate = Fixture::MaintenanceGate;
    const TestCommand operation = command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Operation);
    Gate missingStatus(nullptr, nullptr, Fixture::resolveClass, &fixture,
                       Fixture::resolveKind, &fixture);
    Gate missingClass(Fixture::readStatus, &fixture, nullptr, nullptr,
                      Fixture::resolveKind, &fixture);
    Gate missingKind(Fixture::readStatus, &fixture, Fixture::resolveClass,
                     &fixture, nullptr, nullptr);
    TEST_ASSERT_FALSE(missingStatus.allows(operation));
    TEST_ASSERT_FALSE(missingClass.allows(operation));
    TEST_ASSERT_FALSE(missingKind.allows(operation));
    TEST_ASSERT_FALSE(Gate::callback(operation, nullptr));

    fixture.statusAvailable = false;
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(operation));
    fixture.statusAvailable = true;
    fixture.classAvailable = false;
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(operation));
    fixture.classAvailable = true;
    fixture.kindAvailable = false;
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(operation));
    fixture.kindAvailable = true;
    fixture.status.operational = System::OperationalState::MAINTENANCE;
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(command(
        static_cast<Commands::CommandClass>(0xFFU),
        Commands::MaintenanceCommandKind::Operation)));
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(command(
        Commands::CommandClass::NormalDomain,
        Commands::MaintenanceCommandKind::Operation)));
    TEST_ASSERT_FALSE(fixture.maintenancePolicy.allows(command(
        Commands::CommandClass::SystemRecovery,
        Commands::MaintenanceCommandKind::Operation)));
}

void testOperationalPolicyDoesNotUseHealthOrSafety() {
    Fixture fixture;
    fixture.status.operational = System::OperationalState::MAINTENANCE;
    fixture.status.health = System::HealthState::FAULT;
    fixture.status.safety = System::SafetyState::LOCKED;
    TEST_ASSERT_TRUE(fixture.maintenancePolicy.allows(command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Operation)));
    // This gate decides only operational policy. The pipeline safety stage
    // remains independently responsible for its own decision.
}

void testMaintenanceClassIsIsolatedAcrossCommandPaths() {
    Fixture fixture;
    const TestCommand maintenanceEnter = command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Enter);
    const TestCommand normalCommand = command(
        Commands::CommandClass::NormalDomain,
        Commands::MaintenanceCommandKind::Operation);
    const TestCommand recoveryCommand = command(
        Commands::CommandClass::SystemRecovery,
        Commands::MaintenanceCommandKind::Exit);

    Commands::CommandPipeline<TestCommand> normalPath(
        fixture.pipelineConfig(Fixture::PolicyGate::callback,
                                &fixture.normalPolicy));
    Commands::CommandPipeline<TestCommand> recoveryPath(
        fixture.pipelineConfig(Fixture::PolicyGate::callback,
                                &fixture.recoveryPolicy));
    Commands::CommandPipeline<TestCommand> maintenancePath(
        fixture.pipelineConfig(Fixture::MaintenanceGate::callback,
                                &fixture.maintenancePolicy));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(normalPath.execute(maintenanceEnter).outcome()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(recoveryPath.execute(maintenanceEnter).outcome()));
    fixture.status.operational = System::OperationalState::MAINTENANCE;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(normalPath.execute(normalCommand).outcome()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(maintenancePath.execute(normalCommand).outcome()));
    fixture.status.operational = System::OperationalState::ERROR;
    TEST_ASSERT_TRUE(fixture.recoveryPolicy.allows(recoveryCommand));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(maintenancePath.execute(recoveryCommand).outcome()));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.safetyCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.handlerCalls);
}

void testMaintenanceOperationPolicyShortCircuitsBeforeSafetyAndHandler() {
    Fixture fixture;
    Commands::CommandPipeline<TestCommand> maintenancePath(
        fixture.pipelineConfig(Fixture::MaintenanceGate::callback,
                                &fixture.maintenancePolicy));
    const TestCommand operation = command(
        Commands::CommandClass::Maintenance,
        Commands::MaintenanceCommandKind::Operation);

    fixture.status.operational = System::OperationalState::RUNNING;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::BlockedByPolicy),
        static_cast<int>(maintenancePath.execute(operation).outcome()));
    TEST_ASSERT_EQUAL_UINT(0U, fixture.safetyCalls);
    TEST_ASSERT_EQUAL_UINT(0U, fixture.handlerCalls);

    fixture.status.operational = System::OperationalState::MAINTENANCE;
    fixture.status.health = System::HealthState::DEGRADED;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Commands::CommandExecutionOutcome::Handled),
        static_cast<int>(maintenancePath.execute(operation).outcome()));
    TEST_ASSERT_EQUAL_UINT(1U, fixture.safetyCalls);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.handlerCalls);
}

} // namespace

void runRuntimeMaintenanceCommandPolicyGateTests() {
    RUN_TEST(testMaintenancePolicyMatrixAndIdempotencyDelegation);
    RUN_TEST(testMaintenancePolicyReadsLiveOperationalStatus);
    RUN_TEST(testMaintenancePolicyFailsClosedForMissingAndInvalidInputs);
    RUN_TEST(testOperationalPolicyDoesNotUseHealthOrSafety);
    RUN_TEST(testMaintenanceClassIsIsolatedAcrossCommandPaths);
    RUN_TEST(testMaintenanceOperationPolicyShortCircuitsBeforeSafetyAndHandler);
}
