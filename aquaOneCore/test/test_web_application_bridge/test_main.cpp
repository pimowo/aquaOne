#include <unity.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>

#include "AquaCore/Web/Esp32ActionBridgeSynchronizer.h"
#include "AquaCore/Web/WebApplicationBridge.h"

namespace {
struct Request {
    explicit constexpr Request(uint32_t initial = 0U) : value(initial) {}
    uint32_t value;
};
enum class Result : uint8_t { None, Done };
Result execute(const Request&, void*) { return Result::Done; }
}

void setup() {
    UNITY_BEGIN();
    AquaCore::Web::Esp32ActionBridgeSynchronizer synchronizer;
    AquaCore::Web::WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    AquaCore::Web::ApplicationBridgeToken token {};
    if (bridge.submit(Request {1U}, token) ==
            AquaCore::Web::ApplicationBridgeSubmitResult::Accepted) {
        bridge.processOne(execute);
        Result result = Result::None;
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

#include "AquaCore/Web/WebApplicationBridge.h"

using namespace AquaCore::Web;

namespace {

struct Request {
    explicit constexpr Request(uint32_t initial = 0U) : value(initial) {}
    uint32_t value;
};
enum class Result : uint8_t { None, Applied, NoChange, StorageFailure };

class Synchronizer final : public ActionBridgeSynchronizer {
public:
    static constexpr size_t CAPACITY = 2U;
    bool lock() override { mutex.lock(); held.store(true); return true; }
    void unlock() override { held.store(false); mutex.unlock(); }
    size_t completionSlotCapacity() const override { return CAPACITY; }
    bool prepareCompletion(size_t slot) override {
        if (slot >= CAPACITY || failPrepare) return false;
        std::lock_guard<std::mutex> guard(signals[slot].mutex);
        signals[slot].ready = false;
        return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t timeoutMs) override {
        if (slot >= CAPACITY || failWait) return ActionBridgeWaitStatus::Failure;
        waiting.store(true);
        std::unique_lock<std::mutex> lock(signals[slot].mutex);
        const bool ready = signals[slot].cv.wait_for(
            lock, std::chrono::milliseconds(timeoutMs),
            [&]() { return signals[slot].ready; }
        );
        waiting.store(false);
        return ready ? ActionBridgeWaitStatus::Signaled
                     : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override {
        if (slot >= CAPACITY) return false;
        {
            std::lock_guard<std::mutex> guard(signals[slot].mutex);
            signals[slot].ready = true;
        }
        signals[slot].cv.notify_one();
        return true;
    }
    struct Signal { std::mutex mutex; std::condition_variable cv; bool ready = false; };
    std::mutex mutex;
    Signal signals[CAPACITY];
    std::atomic<bool> held {false};
    std::atomic<bool> waiting {false};
    bool failPrepare = false;
    bool failWait = false;
};

struct Context {
    explicit Context(Synchronizer* value) : synchronizer(value) {}
    Synchronizer* synchronizer = nullptr;
    uint32_t seen = 0U;
    uint32_t order[4] {};
    size_t calls = 0U;
    std::atomic<bool> started {false};
    std::atomic<bool> release {false};
    bool block = false;
};

Result execute(const Request& request, void* opaque) {
    Context& context = *static_cast<Context*>(opaque);
    TEST_ASSERT_FALSE(context.synchronizer->held.load());
    context.seen = request.value;
    context.order[context.calls] = request.value;
    ++context.calls;
    context.started.store(true);
    while (context.block && !context.release.load()) std::this_thread::yield();
    if (request.value == 1U) return Result::Applied;
    if (request.value == 2U) return Result::NoChange;
    return Result::StorageFailure;
}

void test_owned_request_and_exact_result() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    Request request {2U};
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(request, token));
    request.value = 99U;
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    Result result = Result::None;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed,
                      bridge.wait(token, 0U, result));
    TEST_ASSERT_EQUAL_UINT32(2U, context.seen);
    TEST_ASSERT_EQUAL(Result::NoChange, result);
}

void test_empty_owned_fifo_and_one_process_per_call() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    TEST_ASSERT_EQUAL_UINT32(0U, bridge.pendingCount());
    TEST_ASSERT_FALSE(bridge.processOne(execute, &context));
    Request firstRequest {1U};
    ApplicationBridgeToken first {}, second {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(firstRequest, first));
    firstRequest.value = 99U;
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {2U}, second));
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    TEST_ASSERT_EQUAL_UINT32(1U, context.calls);
    TEST_ASSERT_EQUAL_UINT32(1U, context.order[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, bridge.pendingCount());
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    TEST_ASSERT_EQUAL_UINT32(2U, context.calls);
    TEST_ASSERT_EQUAL_UINT32(2U, context.order[1]);
}

void test_capacity_is_bounded() {
    Synchronizer synchronizer;
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken first {}, second {}, rejected {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, first));
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {2U}, second));
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::QueueFull,
                      bridge.submit(Request {3U}, rejected));
}

void test_timeout_does_not_cancel_or_retry() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, token));
    Result result = Result::None;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::TimedOutAccepted,
                      bridge.wait(token, 0U, result));
    TEST_ASSERT_EQUAL_UINT(0U, context.calls);
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    TEST_ASSERT_EQUAL_UINT(1U, context.calls);
    ApplicationBridgeToken next {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {3U}, next));
}

void test_abandon_closes_waiter_and_late_completion_reclaims() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {3U}, token));
    TEST_ASSERT_EQUAL(ApplicationBridgeAbandonResult::Abandoned,
                      bridge.abandon(token));
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    TEST_ASSERT_EQUAL_UINT(1U, context.calls);
}

void test_completion_before_wait_and_aba_token_rejection() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken oldToken {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, oldToken));
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    Result result = Result::None;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed,
                      bridge.wait(oldToken, 0U, result));
    ApplicationBridgeToken newToken {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {2U}, newToken));
    TEST_ASSERT_EQUAL_UINT32(oldToken.slotIndex, newToken.slotIndex);
    TEST_ASSERT_NOT_EQUAL_UINT64(oldToken.generation, newToken.generation);
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::InvalidToken,
                      bridge.wait(oldToken, 0U, result));
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed,
                      bridge.wait(newToken, 0U, result));
    TEST_ASSERT_EQUAL(Result::NoChange, result);
}

void test_wait_before_completion_is_signaled() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, token));
    Result result = Result::None;
    ApplicationBridgeWaitResult waited = ApplicationBridgeWaitResult::InvalidToken;
    std::thread waiter([&]() { waited = bridge.wait(token, 100U, result); });
    while (!synchronizer.waiting.load()) std::this_thread::yield();
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    waiter.join();
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed, waited);
    TEST_ASSERT_EQUAL(Result::Applied, result);
}

void test_timeout_while_processing_reclaims_after_late_completion() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    context.block = true;
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, token));
    std::thread application([&]() { bridge.processOne(execute, &context); });
    while (!context.started.load()) std::this_thread::yield();
    Result result = Result::None;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::TimedOutAccepted,
                      bridge.wait(token, 0U, result));
    context.release.store(true);
    application.join();
    ApplicationBridgeToken reused {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {2U}, reused));
}

void test_capacity_mismatch_and_prepare_failure_reject_before_acceptance() {
    Synchronizer synchronizer;
    WebApplicationBridge<Request, Result, 3U> oversized(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_FALSE(oversized.isCapacitySupported());
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::SynchronizationFailure,
                      oversized.submit(Request {1U}, token));
    synchronizer.failPrepare = true;
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::SynchronizationFailure,
                      bridge.submit(Request {1U}, token));
    TEST_ASSERT_EQUAL_UINT32(0U, bridge.pendingCount());
}

void test_wait_failure_detaches_waiter_and_late_completion_reclaims() {
    Synchronizer synchronizer;
    Context context {&synchronizer};
    WebApplicationBridge<Request, Result, 2U> bridge(synchronizer);
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {1U}, token));
    synchronizer.failWait = true;
    Result result = Result::None;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::SynchronizationFailure,
                      bridge.wait(token, 10U, result));
    TEST_ASSERT_TRUE(bridge.processOne(execute, &context));
    ApplicationBridgeToken reused {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      bridge.submit(Request {2U}, reused));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_owned_request_and_exact_result);
    RUN_TEST(test_empty_owned_fifo_and_one_process_per_call);
    RUN_TEST(test_capacity_is_bounded);
    RUN_TEST(test_timeout_does_not_cancel_or_retry);
    RUN_TEST(test_abandon_closes_waiter_and_late_completion_reclaims);
    RUN_TEST(test_completion_before_wait_and_aba_token_rejection);
    RUN_TEST(test_wait_before_completion_is_signaled);
    RUN_TEST(test_timeout_while_processing_reclaims_after_late_completion);
    RUN_TEST(test_capacity_mismatch_and_prepare_failure_reject_before_acceptance);
    RUN_TEST(test_wait_failure_detaches_waiter_and_late_completion_reclaims);
    return UNITY_END();
}

#endif
