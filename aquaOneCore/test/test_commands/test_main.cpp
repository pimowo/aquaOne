#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <type_traits>

#include <unity.h>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Commands/DomainCommandResult.h"

namespace {

using AquaCore::Commands::DomainCommandResult;
using AquaCore::Commands::CommandExecutionOutcome;
using AquaCore::Commands::CommandExecutionResult;
using AquaCore::Commands::CommandPipeline;
using AquaCore::Commands::CommandPipelineConfig;

static_assert(
    std::is_enum<DomainCommandResult>::value,
    "DomainCommandResult must be an enum"
);
static_assert(
    std::is_trivially_copyable<CommandExecutionResult>::value,
    "CommandExecutionResult must be trivially copyable"
);

struct TestCommand {
    uint32_t value;
    uint8_t mode;
};

struct CallTrace {
    char calls[8] {};
    size_t count = 0U;

    void add(char marker) {
        calls[count++] = marker;
    }
};

struct StageContext {
    CallTrace* trace = nullptr;
    size_t calls = 0U;
    bool allow = true;
    DomainCommandResult result = DomainCommandResult::Completed;
    uint32_t observedValue = 0U;
};

bool validateCommand(const TestCommand& command, void* context) {
    StageContext& stage = *static_cast<StageContext*>(context);
    ++stage.calls;
    stage.observedValue = command.value;
    stage.trace->add('V');
    return stage.allow;
}

bool applyPolicy(const TestCommand& command, void* context) {
    StageContext& stage = *static_cast<StageContext*>(context);
    ++stage.calls;
    stage.observedValue = command.value;
    stage.trace->add('P');
    return stage.allow;
}

bool applySafety(const TestCommand& command, void* context) {
    StageContext& stage = *static_cast<StageContext*>(context);
    ++stage.calls;
    stage.observedValue = command.value;
    stage.trace->add('S');
    return stage.allow;
}

DomainCommandResult handleCommand(
    const TestCommand& command,
    void* context
) {
    StageContext& stage = *static_cast<StageContext*>(context);
    ++stage.calls;
    stage.observedValue = command.value;
    stage.trace->add('H');
    return stage.result;
}

bool statelessValidator(const TestCommand&, void*) {
    return true;
}

bool statelessGate(const TestCommand&, void*) {
    return true;
}

DomainCommandResult statelessHandler(const TestCommand&, void*) {
    return DomainCommandResult::Completed;
}

struct PipelineFixture {
    CallTrace trace {};
    StageContext validator {};
    StageContext policy {};
    StageContext safety {};
    StageContext handler {};

    PipelineFixture() {
        validator.trace = &trace;
        policy.trace = &trace;
        safety.trace = &trace;
        handler.trace = &trace;
    }

    CommandPipelineConfig<TestCommand> config() {
        CommandPipelineConfig<TestCommand> value {};
        value.validator = validateCommand;
        value.validatorContext = &validator;
        value.policy = applyPolicy;
        value.policyContext = &policy;
        value.safety = applySafety;
        value.safetyContext = &safety;
        value.handler = handleCommand;
        value.handlerContext = &handler;
        return value;
    }
};

void assertNoDomainResult(
    const CommandExecutionResult& result,
    CommandExecutionOutcome expected
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(result.outcome())
    );
    TEST_ASSERT_FALSE(result.hasDomainResult());
    TEST_ASSERT_NULL(result.domainResult());
}
static_assert(
    std::is_trivially_copyable<DomainCommandResult>::value,
    "DomainCommandResult must be trivially copyable"
);
static_assert(
    !std::is_convertible<DomainCommandResult, uint8_t>::value,
    "DomainCommandResult must remain a scoped enum"
);

void test_all_domain_command_results_are_distinct() {
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::Completed),
        static_cast<uint8_t>(DomainCommandResult::Rejected)
    );
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::Rejected),
        static_cast<uint8_t>(DomainCommandResult::InvalidState)
    );
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::InvalidState),
        static_cast<uint8_t>(DomainCommandResult::OperationStarted)
    );
}

void test_domain_command_result_copy_preserves_value() {
    const DomainCommandResult original =
        DomainCommandResult::OperationStarted;
    const DomainCommandResult copied = original;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(original),
        static_cast<uint8_t>(copied)
    );
}

void test_domain_command_result_names_are_stable() {
    TEST_ASSERT_EQUAL_STRING(
        "COMPLETED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::Completed
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "REJECTED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::Rejected
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "INVALID_STATE",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::InvalidState
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "OPERATION_STARTED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::OperationStarted
        )
    );
}

void test_unknown_domain_command_result_has_fallback_name() {
    TEST_ASSERT_EQUAL_STRING(
        "UNKNOWN",
        AquaCore::Commands::domainCommandResultName(
            static_cast<DomainCommandResult>(0xFFU)
        )
    );
}

void test_pipeline_executes_in_exact_order() {
    PipelineFixture fixture;
    const TestCommand command {42U, 3U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    const CommandExecutionResult result = pipeline.execute(command);

    TEST_ASSERT_TRUE(pipeline.isValid());
    TEST_ASSERT_EQUAL_UINT32(4U, fixture.trace.count);
    TEST_ASSERT_EQUAL_MEMORY("VPSH", fixture.trace.calls, 4U);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.validator.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.policy.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.safety.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.handler.calls);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandExecutionOutcome::Handled),
        static_cast<uint8_t>(result.outcome())
    );
}

void test_invalid_command_short_circuits() {
    PipelineFixture fixture;
    fixture.validator.allow = false;
    const TestCommand command {1U, 0U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    const CommandExecutionResult result = pipeline.execute(command);

    assertNoDomainResult(
        result,
        CommandExecutionOutcome::InvalidCommand
    );
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.validator.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.policy.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.safety.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.handler.calls);
}

void test_policy_denial_short_circuits() {
    PipelineFixture fixture;
    fixture.policy.allow = false;
    const TestCommand command {2U, 0U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    const CommandExecutionResult result = pipeline.execute(command);

    assertNoDomainResult(
        result,
        CommandExecutionOutcome::BlockedByPolicy
    );
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.validator.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.policy.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.safety.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.handler.calls);
}

void test_safety_block_short_circuits() {
    PipelineFixture fixture;
    fixture.safety.allow = false;
    const TestCommand command {3U, 0U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    const CommandExecutionResult result = pipeline.execute(command);

    assertNoDomainResult(
        result,
        CommandExecutionOutcome::BlockedBySafety
    );
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.validator.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.policy.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.safety.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.handler.calls);
}

void test_all_domain_results_are_propagated() {
    const DomainCommandResult expected[] = {
        DomainCommandResult::Completed,
        DomainCommandResult::Rejected,
        DomainCommandResult::InvalidState,
        DomainCommandResult::OperationStarted
    };

    for (size_t index = 0U; index < 4U; ++index) {
        PipelineFixture fixture;
        fixture.handler.result = expected[index];
        const TestCommand command {
            static_cast<uint32_t>(index + 10U),
            0U
        };
        CommandPipeline<TestCommand> pipeline(fixture.config());

        const CommandExecutionResult result = pipeline.execute(command);

        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(CommandExecutionOutcome::Handled),
            static_cast<uint8_t>(result.outcome())
        );
        TEST_ASSERT_TRUE(result.hasDomainResult());
        TEST_ASSERT_NOT_NULL(result.domainResult());
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expected[index]),
            static_cast<uint8_t>(*result.domainResult())
        );
    }
}

void test_missing_callbacks_fail_closed_before_execution() {
    const TestCommand command {20U, 0U};
    for (uint8_t missing = 0U; missing < 4U; ++missing) {
        PipelineFixture fixture;
        CommandPipelineConfig<TestCommand> config = fixture.config();
        if (missing == 0U) {
            config.validator = nullptr;
        } else if (missing == 1U) {
            config.policy = nullptr;
        } else if (missing == 2U) {
            config.safety = nullptr;
        } else {
            config.handler = nullptr;
        }
        CommandPipeline<TestCommand> pipeline(config);

        const CommandExecutionResult result = pipeline.execute(command);

        TEST_ASSERT_FALSE(pipeline.isValid());
        assertNoDomainResult(
            result,
            CommandExecutionOutcome::InvalidPipeline
        );
        TEST_ASSERT_EQUAL_UINT32(0U, fixture.trace.count);
    }
}

void test_stateless_callbacks_accept_null_contexts() {
    const TestCommand command {21U, 0U};
    CommandPipelineConfig<TestCommand> config {};
    config.validator = statelessValidator;
    config.policy = statelessGate;
    config.safety = statelessGate;
    config.handler = statelessHandler;
    CommandPipeline<TestCommand> pipeline(config);

    const CommandExecutionResult result = pipeline.execute(command);

    TEST_ASSERT_TRUE(pipeline.isValid());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandExecutionOutcome::Handled),
        static_cast<uint8_t>(result.outcome())
    );
    TEST_ASSERT_TRUE(result.hasDomainResult());
    TEST_ASSERT_NOT_NULL(result.domainResult());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DomainCommandResult::Completed),
        static_cast<uint8_t>(*result.domainResult())
    );
}

void test_invalid_handler_result_is_not_exposed_as_domain_result() {
    PipelineFixture fixture;
    fixture.handler.result = static_cast<DomainCommandResult>(0xFFU);
    const TestCommand command {21U, 0U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    const CommandExecutionResult result = pipeline.execute(command);

    assertNoDomainResult(
        result,
        CommandExecutionOutcome::InvalidPipeline
    );
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.handler.calls);
}

void test_pipelines_and_contexts_are_independent() {
    PipelineFixture first;
    PipelineFixture second;
    second.policy.allow = false;
    const TestCommand firstCommand {31U, 1U};
    const TestCommand secondCommand {32U, 2U};
    CommandPipeline<TestCommand> firstPipeline(first.config());
    CommandPipeline<TestCommand> secondPipeline(second.config());

    const CommandExecutionResult firstResult =
        firstPipeline.execute(firstCommand);
    const CommandExecutionResult secondResult =
        secondPipeline.execute(secondCommand);

    TEST_ASSERT_TRUE(firstResult.hasDomainResult());
    assertNoDomainResult(
        secondResult,
        CommandExecutionOutcome::BlockedByPolicy
    );
    TEST_ASSERT_EQUAL_UINT32(31U, first.handler.observedValue);
    TEST_ASSERT_EQUAL_UINT32(32U, second.policy.observedValue);
    TEST_ASSERT_EQUAL_UINT32(1U, first.handler.calls);
    TEST_ASSERT_EQUAL_UINT32(0U, second.handler.calls);
}

void test_pipeline_does_not_mutate_input_command() {
    PipelineFixture fixture;
    const TestCommand command {77U, 9U};
    CommandPipeline<TestCommand> pipeline(fixture.config());

    pipeline.execute(command);

    TEST_ASSERT_EQUAL_UINT32(77U, command.value);
    TEST_ASSERT_EQUAL_UINT8(9U, command.mode);
}

void runTests() {
    UNITY_BEGIN();
    RUN_TEST(test_all_domain_command_results_are_distinct);
    RUN_TEST(test_domain_command_result_copy_preserves_value);
    RUN_TEST(test_domain_command_result_names_are_stable);
    RUN_TEST(test_unknown_domain_command_result_has_fallback_name);
    RUN_TEST(test_pipeline_executes_in_exact_order);
    RUN_TEST(test_invalid_command_short_circuits);
    RUN_TEST(test_policy_denial_short_circuits);
    RUN_TEST(test_safety_block_short_circuits);
    RUN_TEST(test_all_domain_results_are_propagated);
    RUN_TEST(test_missing_callbacks_fail_closed_before_execution);
    RUN_TEST(test_stateless_callbacks_accept_null_contexts);
    RUN_TEST(test_invalid_handler_result_is_not_exposed_as_domain_result);
    RUN_TEST(test_pipelines_and_contexts_are_independent);
    RUN_TEST(test_pipeline_does_not_mutate_input_command);
}

} // namespace

#if defined(ARDUINO)

void setup() {
    delay(2000);
    runTests();
    UNITY_END();
}

void loop() {
}

#else

int main() {
    runTests();
    return UNITY_END();
}

#endif
