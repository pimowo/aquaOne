#include <unity.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Web/Esp32ActionBridgeSynchronizer.h"
#include "AquaCore/Web/WebActionBridge.h"

namespace {
struct Esp32TestCommand { uint32_t value; };

bool accepts(const Esp32TestCommand&, void*) { return true; }
AquaCore::Commands::DomainCommandResult execute(const Esp32TestCommand&, void*) {
    return AquaCore::Commands::DomainCommandResult::Completed;
}
} // namespace

void setup() {
    UNITY_BEGIN();
    AquaCore::Web::Esp32ActionBridgeSynchronizer synchronizer;
    AquaCore::Commands::CommandPipelineConfig<Esp32TestCommand> config {};
    config.validator = accepts;
    config.policy = accepts;
    config.safety = accepts;
    config.handler = execute;
    AquaCore::Commands::CommandPipeline<Esp32TestCommand> pipeline(config);
    AquaCore::Web::WebActionBridge<
        Esp32TestCommand,
        AquaCore::Web::Esp32ActionBridgeSynchronizer::MAX_SLOTS
    > bridge(synchronizer, pipeline);
    AquaCore::Web::ActionBridgeToken token {};
    if (synchronizer.isReady() &&
        bridge.submit(Esp32TestCommand {1U}, token) ==
            AquaCore::Web::ActionBridgeSubmitResult::Accepted) {
        bridge.processOne();
        AquaCore::Commands::CommandExecutionResult result =
            AquaCore::Commands::CommandExecutionResult::invalidPipeline();
        bridge.wait(token, 0U, result);
    }
    UNITY_END();
}
void loop() {}

#else

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Web/WebActionBridge.h"

using AquaCore::Commands::CommandExecutionOutcome;
using AquaCore::Commands::CommandExecutionResult;
using AquaCore::Commands::CommandPipeline;
using AquaCore::Commands::CommandPipelineConfig;
using AquaCore::Commands::DomainCommandResult;
using AquaCore::Web::ActionBridgeSubmitResult;
using AquaCore::Web::ActionBridgeSynchronizer;
using AquaCore::Web::ActionBridgeToken;
using AquaCore::Web::ActionBridgeWaitStatus;
using AquaCore::Web::ActionBridgeWaitResult;
using AquaCore::Web::WebActionBridge;

namespace {

struct TestCommand {
    uint32_t sequence;
    uint8_t outcome;
};

class NativeBridgeSynchronizer final : public ActionBridgeSynchronizer {
public:
    static constexpr size_t SLOT_COUNT = 3U;

    bool lock() override {
        metadata_.lock();
        held_.store(true);
        return true;
    }
    void unlock() override {
        held_.store(false);
        metadata_.unlock();
    }
    size_t completionSlotCapacity() const override { return SLOT_COUNT; }
    bool prepareCompletion(size_t slot) override {
        if (slot >= SLOT_COUNT) { return false; }
        std::lock_guard<std::mutex> guard(signals_[slot].mutex);
        signals_[slot].signaled = false;
        return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t timeoutMs) override {
        if (slot >= SLOT_COUNT) { return ActionBridgeWaitStatus::Failure; }
        waiting_.store(true);
        std::unique_lock<std::mutex> lock(signals_[slot].mutex);
        const bool received = signals_[slot].event.wait_for(
            lock, std::chrono::milliseconds(timeoutMs),
            [&]() { return signals_[slot].signaled; }
        );
        waiting_.store(false);
        return received ? ActionBridgeWaitStatus::Signaled
                        : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override {
        if (slot >= SLOT_COUNT) { return false; }
        {
            std::lock_guard<std::mutex> guard(signals_[slot].mutex);
            signals_[slot].signaled = true;
        }
        signals_[slot].event.notify_one();
        return true;
    }
    bool isHeld() const { return held_.load(); }
    bool isWaiting() const { return waiting_.load(); }

private:
    struct Signal {
        std::mutex mutex;
        std::condition_variable event;
        bool signaled = false;
    };
    std::mutex metadata_;
    Signal signals_[SLOT_COUNT];
    std::atomic<bool> held_ {false};
    std::atomic<bool> waiting_ {false};
};

struct Fixture {
    NativeBridgeSynchronizer* synchronizer = nullptr;
    uint32_t executed[16] {};
    size_t executionCount = 0U;
    std::atomic<bool> handlerStarted {false};
    std::atomic<bool> releaseHandler {false};
    bool blockHandler = false;
};

bool validator(const TestCommand& command, void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    TEST_ASSERT_FALSE(fixture.synchronizer->isHeld());
    return command.outcome != 4U;
}
bool policy(const TestCommand& command, void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    TEST_ASSERT_FALSE(fixture.synchronizer->isHeld());
    return command.outcome != 5U;
}
bool safety(const TestCommand& command, void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    TEST_ASSERT_FALSE(fixture.synchronizer->isHeld());
    return command.outcome != 6U;
}
DomainCommandResult handler(const TestCommand& command, void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    TEST_ASSERT_FALSE(fixture.synchronizer->isHeld());
    fixture.executed[fixture.executionCount++] = command.sequence;
    fixture.handlerStarted.store(true);
    while (fixture.blockHandler && !fixture.releaseHandler.load()) {
        std::this_thread::yield();
    }
    switch (command.outcome) {
        case 1U: return DomainCommandResult::Rejected;
        case 2U: return DomainCommandResult::InvalidState;
        case 3U: return DomainCommandResult::OperationStarted;
        default: return DomainCommandResult::Completed;
    }
}

struct BridgeFixture {
    NativeBridgeSynchronizer synchronizer;
    Fixture callbacks;
    CommandPipelineConfig<TestCommand> config;
    CommandPipeline<TestCommand> pipeline;
    WebActionBridge<TestCommand, NativeBridgeSynchronizer::SLOT_COUNT> bridge;

    BridgeFixture() : config(makeConfig(callbacks)), pipeline(config), bridge(synchronizer, pipeline) {
        callbacks.synchronizer = &synchronizer;
        // The callback context points to callbacks itself; its synchronizer is
        // installed before any submit/process operation.
    }

private:
    static CommandPipelineConfig<TestCommand> makeConfig(Fixture& callbacks) {
        CommandPipelineConfig<TestCommand> value {};
        value.validator = validator;
        value.validatorContext = &callbacks;
        value.policy = policy;
        value.policyContext = &callbacks;
        value.safety = safety;
        value.safetyContext = &callbacks;
        value.handler = handler;
        value.handlerContext = &callbacks;
        return value;
    }
};

void assertHandled(const CommandExecutionResult& result, DomainCommandResult domain) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandExecutionOutcome::Handled),
                            static_cast<uint8_t>(result.outcome()));
    TEST_ASSERT_TRUE(result.hasDomainResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(domain),
                            static_cast<uint8_t>(*result.domainResult()));
}

void test_empty_and_owned_submit_fifo() {
    BridgeFixture fixture;
    TEST_ASSERT_EQUAL_UINT(0U, fixture.bridge.pendingCount());
    ActionBridgeToken first {};
    ActionBridgeToken second {};
    TestCommand command {10U, 0U};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(command, first)));
    command.sequence = 99U;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {11U, 0U}, second)));
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    TEST_ASSERT_EQUAL_UINT(2U, fixture.callbacks.executionCount);
    TEST_ASSERT_EQUAL_UINT32(10U, fixture.callbacks.executed[0]);
    TEST_ASSERT_EQUAL_UINT32(11U, fixture.callbacks.executed[1]);
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(fixture.bridge.wait(first, 0U, result)));
    assertHandled(result, DomainCommandResult::Completed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(fixture.bridge.wait(second, 0U, result)));
}

void test_pipeline_results_are_preserved() {
    const uint8_t outcomes[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U};
    for (size_t index = 0U; index < sizeof(outcomes); ++index) {
        BridgeFixture fixture;
        ActionBridgeToken token {};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                                static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, outcomes[index]}, token)));
        TEST_ASSERT_TRUE(fixture.bridge.processOne());
        CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                                static_cast<uint8_t>(fixture.bridge.wait(token, 0U, result)));
        if (outcomes[index] < 4U) {
            const DomainCommandResult expected[] = {
                DomainCommandResult::Completed, DomainCommandResult::Rejected,
                DomainCommandResult::InvalidState, DomainCommandResult::OperationStarted
            };
            assertHandled(result, expected[outcomes[index]]);
        } else {
            const CommandExecutionOutcome expected[] = {
                CommandExecutionOutcome::InvalidCommand,
                CommandExecutionOutcome::BlockedByPolicy,
                CommandExecutionOutcome::BlockedBySafety
            };
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected[outcomes[index] - 4U]),
                                    static_cast<uint8_t>(result.outcome()));
        }
    }
}

void test_invalid_pipeline_is_preserved() {
    NativeBridgeSynchronizer synchronizer;
    CommandPipelineConfig<TestCommand> invalidConfig {};
    CommandPipeline<TestCommand> pipeline(invalidConfig);
    WebActionBridge<TestCommand, NativeBridgeSynchronizer::SLOT_COUNT> bridge(synchronizer, pipeline);
    ActionBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(bridge.submit(TestCommand {1U, 0U}, token)));
    TEST_ASSERT_TRUE(bridge.processOne());
    CommandExecutionResult result = CommandExecutionResult::handled(DomainCommandResult::Completed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(bridge.wait(token, 0U, result)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandExecutionOutcome::InvalidPipeline),
                            static_cast<uint8_t>(result.outcome()));
}

void test_capacity_timeout_and_late_completion_reclaim() {
    BridgeFixture fixture;
    ActionBridgeToken tokens[NativeBridgeSynchronizer::SLOT_COUNT] {};
    for (size_t i = 0U; i < NativeBridgeSynchronizer::SLOT_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                                static_cast<uint8_t>(fixture.bridge.submit(TestCommand {static_cast<uint32_t>(i), 0U}, tokens[i])));
    }
    ActionBridgeToken rejected {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::QueueFull),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {99U, 0U}, rejected)));
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::TimedOutAccepted),
                            static_cast<uint8_t>(fixture.bridge.wait(tokens[0], 0U, result)));
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    ActionBridgeToken reused {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {100U, 0U}, reused)));
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.callbacks.executed[0]);
}

void test_completion_before_wait_and_aba_token_rejection() {
    BridgeFixture fixture;
    ActionBridgeToken first {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, first)));
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(fixture.bridge.wait(first, 0U, result)));
    ActionBridgeToken second {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {2U, 0U}, second)));
    TEST_ASSERT_EQUAL_UINT(first.slotIndex, second.slotIndex);
    TEST_ASSERT_NOT_EQUAL_UINT64(first.generation, second.generation);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::InvalidToken),
                            static_cast<uint8_t>(fixture.bridge.wait(first, 0U, result)));
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(fixture.bridge.wait(second, 0U, result)));
    assertHandled(result, DomainCommandResult::Completed);
}

void test_timeout_while_processing_reclaims_after_late_completion() {
    BridgeFixture fixture;
    fixture.callbacks.blockHandler = true;
    ActionBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, token)));
    std::thread application([&]() { fixture.bridge.processOne(); });
    while (!fixture.callbacks.handlerStarted.load()) { std::this_thread::yield(); }
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::TimedOutAccepted),
                            static_cast<uint8_t>(fixture.bridge.wait(token, 0U, result)));
    fixture.callbacks.releaseHandler.store(true);
    application.join();
    ActionBridgeToken next {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {2U, 0U}, next)));
}

void test_completion_timeout_race_returns_completed_after_final_check() {
    BridgeFixture fixture;
    ActionBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, token)));
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(fixture.bridge.wait(token, 0U, result)));
    assertHandled(result, DomainCommandResult::Completed);
}

void test_wait_before_completion_is_signaled() {
    BridgeFixture fixture;
    ActionBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, token)));
    ActionBridgeWaitResult waitResult = ActionBridgeWaitResult::InvalidToken;
    CommandExecutionResult result = CommandExecutionResult::invalidPipeline();
    std::thread waiter([&]() {
        waitResult = fixture.bridge.wait(token, 100U, result);
    });
    while (!fixture.synchronizer.isWaiting()) { std::this_thread::yield(); }
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    waiter.join();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeWaitResult::Completed),
                            static_cast<uint8_t>(waitResult));
    assertHandled(result, DomainCommandResult::Completed);
}

void test_explicit_abandon_prevents_submit_without_wait_leak() {
    BridgeFixture fixture;
    ActionBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, token)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(AquaCore::Web::ActionBridgeAbandonResult::Abandoned),
        static_cast<uint8_t>(fixture.bridge.abandon(token))
    );
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    ActionBridgeToken next {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(TestCommand {2U, 0U}, next)));
}

void test_capacity_mismatch_rejects_before_acceptance() {
    NativeBridgeSynchronizer synchronizer;
    CommandPipelineConfig<TestCommand> config {};
    CommandPipeline<TestCommand> pipeline(config);
    WebActionBridge<TestCommand, NativeBridgeSynchronizer::SLOT_COUNT + 1U>
        bridge(synchronizer, pipeline);
    ActionBridgeToken token {};
    TEST_ASSERT_FALSE(bridge.isCapacitySupported());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ActionBridgeSubmitResult::SynchronizationFailure),
        static_cast<uint8_t>(bridge.submit(TestCommand {1U, 0U}, token))
    );
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_and_owned_submit_fifo);
    RUN_TEST(test_pipeline_results_are_preserved);
    RUN_TEST(test_invalid_pipeline_is_preserved);
    RUN_TEST(test_capacity_timeout_and_late_completion_reclaim);
    RUN_TEST(test_completion_before_wait_and_aba_token_rejection);
    RUN_TEST(test_timeout_while_processing_reclaims_after_late_completion);
    RUN_TEST(test_completion_timeout_race_returns_completed_after_final_check);
    RUN_TEST(test_wait_before_completion_is_signaled);
    RUN_TEST(test_explicit_abandon_prevents_submit_without_wait_leak);
    RUN_TEST(test_capacity_mismatch_rejects_before_acceptance);
    return UNITY_END();
}

#endif
