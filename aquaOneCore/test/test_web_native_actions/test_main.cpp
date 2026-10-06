#include <unity.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Web/Esp32ActionBridgeSynchronizer.h"
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"
#include "AquaCore/Web/EspIdfWebTransport.h"
#include "AquaCore/Web/NativeActionBoundary.h"
#include "AquaCore/Web/NativeWebService.h"

namespace {
struct CompileCommand { uint32_t value; };
bool accept(const CompileCommand&, void*) { return true; }
AquaCore::Commands::DomainCommandResult handle(const CompileCommand&, void*) {
    return AquaCore::Commands::DomainCommandResult::Completed;
}
void route(void*, const AquaCore::Web::HttpRouteRequest&,
           AquaCore::Web::WebResponseWriter&) {}
} // namespace

void setup() {
    UNITY_BEGIN();
    using namespace AquaCore;
    using namespace AquaCore::Web;
    EspIdfWebTransport transport;
    Esp32SnapshotSynchronizer snapshotSynchronizer;
    PublishedSnapshot<CoreSystemProjection> system(snapshotSynchronizer);
    PublishedSnapshot<CoreDiagnosticsProjection> diagnostics(snapshotSynchronizer);
    NativeWebService service(transport, system, diagnostics);
    HttpRouteOptions options {32U};
    service.addRoute("/api/compile-action", HttpMethod::Post,
                     route, nullptr, options);

    Esp32ActionBridgeSynchronizer actionSynchronizer;
    Commands::CommandPipelineConfig<CompileCommand> config {};
    config.validator = accept;
    config.policy = accept;
    config.safety = accept;
    config.handler = handle;
    Commands::CommandPipeline<CompileCommand> pipeline(config);
    WebActionBridge<CompileCommand, 1U> bridge(actionSynchronizer, pipeline);
    NativeActionBoundary<CompileCommand, 1U> boundary(bridge);
    const NativeActionResult result =
        boundary.submitAndWait(CompileCommand {7U}, 0U);
    if (result.accepted) bridge.processOne();
    UNITY_END();
}
void loop() {}

#else

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Web/NativeActionBoundary.h"
#include "AquaCore/Web/NativeWebService.h"

using namespace AquaCore;
using namespace AquaCore::Commands;
using namespace AquaCore::Web;

namespace {

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t value, ContentType) override {
        status = value;
        body.clear();
        return true;
    }
    bool write(const char* data, size_t length) override {
        body.append(data, length);
        return true;
    }
    bool endResponse() override { ended = true; return true; }
    uint16_t status = 0U;
    std::string body;
    bool ended = false;
};

class FakeTransport final : public HttpServerTransport {
public:
    using HttpServerTransport::addRoute;

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context,
                  const HttpRouteOptions& options) override {
        ++addAttempts;
        if (failNextAdd) {
            failNextAdd = false;
            return false;
        }
        return registry.addRoute(path, method, handler, context, options);
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler,
                            void* context) override {
        return registry.setNotFoundHandler(handler, context);
    }
    bool begin(uint16_t) override {
        ++beginCalls;
        registry.freeze();
        running = true;
        return true;
    }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }

    Writer request(const char* path, HttpMethod method,
                   const char* body = nullptr, size_t length = 0U) {
        Writer writer;
        for (size_t i = 0U; i < registry.size(); ++i) {
            const HttpRouteRegistry::Route* candidate = registry.routeAt(i);
            if (candidate->method != method ||
                std::strcmp(candidate->path, path) != 0) continue;
            if (length > candidate->options.maxBodyLength) {
                writer.beginResponse(413U, ContentType::PlainText);
                writer.writeText("too large");
                writer.endResponse();
                return writer;
            }
            if (length != 0U) std::memcpy(storage, body, length);
            storage[length] = '\0';
            const HttpRouteRequest request {
                method, path, length == 0U ? nullptr : storage, length
            };
            ++handlerCalls;
            candidate->handler(candidate->context, request, writer);
            return writer;
        }
        writer.beginResponse(404U, ContentType::PlainText);
        writer.endResponse();
        return writer;
    }

    HttpRouteRegistry registry;
    char storage[HTTP_NORMAL_BODY_CAPACITY + 1U] {};
    size_t addAttempts = 0U;
    size_t beginCalls = 0U;
    size_t handlerCalls = 0U;
    bool failNextAdd = false;
    bool running = false;
};

struct BodyCapture {
    char value[HTTP_NORMAL_BODY_CAPACITY + 1U] {};
    size_t length = 0U;
    bool nullBody = false;
};

void captureBody(void* context, const HttpRouteRequest& request,
                 WebResponseWriter& response) {
    BodyCapture& capture = *static_cast<BodyCapture*>(context);
    capture.length = request.bodyLength;
    capture.nullBody = request.body == nullptr;
    if (request.bodyLength != 0U) {
        std::memcpy(capture.value, request.body, request.bodyLength);
    }
    response.beginResponse(200U, ContentType::PlainText);
    response.endResponse();
}

class SnapshotLock final : public SnapshotSynchronizer {
public:
    bool lock() override { return true; }
    void unlock() override {}
};

void test_route_body_policy_and_callback_view() {
    FakeTransport transport;
    BodyCapture capture;
    TEST_ASSERT_TRUE(transport.addRoute(
        "/body", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {HTTP_NORMAL_BODY_CAPACITY}
    ));
    TEST_ASSERT_FALSE(transport.addRoute(
        "/get-body", HttpMethod::Get, captureBody, &capture,
        HttpRouteOptions {1U}
    ));
    TEST_ASSERT_FALSE(transport.addRoute(
        "/too-large", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {HTTP_NORMAL_BODY_CAPACITY + 1U}
    ));
    TEST_ASSERT_TRUE(transport.addRoute(
        "/small", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    const char small[9] = {'a', '\0', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    Writer writer = transport.request("/small", HttpMethod::Post, small, 8U);
    TEST_ASSERT_EQUAL_UINT16(200U, writer.status);
    TEST_ASSERT_EQUAL_UINT(8U, capture.length);
    TEST_ASSERT_EQUAL_CHAR('b', capture.value[2U]);
    const size_t smallCalls = transport.handlerCalls;
    writer = transport.request("/small", HttpMethod::Post, small, 9U);
    TEST_ASSERT_EQUAL_UINT16(413U, writer.status);
    TEST_ASSERT_EQUAL_UINT(smallCalls, transport.handlerCalls);

    char exact[HTTP_NORMAL_BODY_CAPACITY];
    std::memset(exact, 'x', sizeof(exact));
    writer = transport.request(
        "/body", HttpMethod::Post, exact, sizeof(exact)
    );
    TEST_ASSERT_EQUAL_UINT16(200U, writer.status);
    TEST_ASSERT_EQUAL_UINT(sizeof(exact), capture.length);
    TEST_ASSERT_EQUAL_CHAR('x', capture.value[sizeof(exact) - 1U]);

    const size_t calls = transport.handlerCalls;
    writer = transport.request(
        "/body", HttpMethod::Post, exact, sizeof(exact) + 1U
    );
    TEST_ASSERT_EQUAL_UINT16(413U, writer.status);
    TEST_ASSERT_EQUAL_UINT(calls, transport.handlerCalls);
}

void test_zero_limit_is_bodyless_and_zero_view_is_null() {
    FakeTransport transport;
    BodyCapture capture;
    TEST_ASSERT_TRUE(transport.addRoute(
        "/bodyless", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {0U}
    ));
    Writer writer = transport.request("/bodyless", HttpMethod::Post);
    TEST_ASSERT_EQUAL_UINT16(200U, writer.status);
    TEST_ASSERT_TRUE(capture.nullBody);
    TEST_ASSERT_EQUAL_UINT(0U, capture.length);
    const size_t calls = transport.handlerCalls;
    writer = transport.request("/bodyless", HttpMethod::Post, "x", 1U);
    TEST_ASSERT_EQUAL_UINT16(413U, writer.status);
    TEST_ASSERT_EQUAL_UINT(calls, transport.handlerCalls);
}

void test_product_routes_reservations_and_freeze() {
    FakeTransport transport;
    SnapshotLock lock;
    PublishedSnapshot<CoreSystemProjection> system(lock);
    PublishedSnapshot<CoreDiagnosticsProjection> diagnostics(lock);
    NativeWebService service(transport, system, diagnostics);
    BodyCapture capture;
    TEST_ASSERT_FALSE(service.addRoute(
        "/api/system", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    TEST_ASSERT_TRUE(service.addRoute(
        "/api/product/action", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    WebConfig config;
    config.enabled = true;
    TEST_ASSERT_TRUE(service.begin(config));
    TEST_ASSERT_FALSE(service.addRoute(
        "/api/product/late", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    service.stop();
    TEST_ASSERT_FALSE(service.addRoute(
        "/api/product/after-stop", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    const size_t calls = transport.handlerCalls;
    Writer writer = transport.request(
        "/api/system", HttpMethod::Get, "x", 1U
    );
    TEST_ASSERT_EQUAL_UINT16(413U, writer.status);
    TEST_ASSERT_EQUAL_UINT(calls, transport.handlerCalls);
}

void test_partial_product_registration_failure_blocks_begin() {
    FakeTransport transport;
    SnapshotLock lock;
    PublishedSnapshot<CoreSystemProjection> system(lock);
    PublishedSnapshot<CoreDiagnosticsProjection> diagnostics(lock);
    NativeWebService service(transport, system, diagnostics);
    BodyCapture capture;
    TEST_ASSERT_TRUE(service.addRoute(
        "/api/product/one", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    transport.failNextAdd = true;
    TEST_ASSERT_FALSE(service.addRoute(
        "/api/product/two", HttpMethod::Post, captureBody, &capture,
        HttpRouteOptions {8U}
    ));
    WebConfig config;
    config.enabled = true;
    TEST_ASSERT_FALSE(service.begin(config));
    TEST_ASSERT_EQUAL_UINT(0U, transport.beginCalls);
}

struct TestCommand {
    uint32_t value;
    uint8_t outcome;
};

class NativeSynchronizer final : public ActionBridgeSynchronizer {
public:
    bool lock() override {
        const size_t call = lockCalls.fetch_add(1U) + 1U;
        if (failLockCall.load() == call ||
            secondFailLockCall.load() == call ||
            thirdFailLockCall.load() == call) return false;
        metadata.lock();
        return true;
    }
    void unlock() override { metadata.unlock(); }
    size_t completionSlotCapacity() const override { return 1U; }
    bool prepareCompletion(size_t slot) override {
        if (slot != 0U) return false;
        std::lock_guard<std::mutex> guard(signalMutex);
        signaled = false;
        return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot,
                                              uint32_t timeoutMs) override {
        if (slot != 0U) return ActionBridgeWaitStatus::Failure;
        if (waitHook != nullptr) {
            void (*hook)(void*) = waitHook;
            waitHook = nullptr;
            hook(waitHookContext);
        }
        waiting.store(true);
        std::unique_lock<std::mutex> lock(signalMutex);
        const bool received = signal.wait_for(
            lock, std::chrono::milliseconds(timeoutMs),
            [&]() { return signaled; }
        );
        waiting.store(false);
        return received ? ActionBridgeWaitStatus::Signaled
                        : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override {
        if (slot != 0U) return false;
        {
            std::lock_guard<std::mutex> guard(signalMutex);
            signaled = true;
        }
        signal.notify_one();
        return true;
    }

    std::mutex metadata;
    std::mutex signalMutex;
    std::condition_variable signal;
    std::atomic<size_t> lockCalls {0U};
    std::atomic<size_t> failLockCall {0U};
    std::atomic<size_t> secondFailLockCall {0U};
    std::atomic<size_t> thirdFailLockCall {0U};
    std::atomic<bool> waiting {false};
    bool signaled = false;
    void (*waitHook)(void*) = nullptr;
    void* waitHookContext = nullptr;
};

struct CallbackState {
    uint32_t executedValue = 0U;
    size_t executionCount = 0U;
    std::thread::id executionThread;
};

bool validator(const TestCommand& command, void*) { return command.outcome != 4U; }
bool policy(const TestCommand& command, void*) { return command.outcome != 5U; }
bool safety(const TestCommand& command, void*) { return command.outcome != 6U; }
DomainCommandResult commandHandler(const TestCommand& command, void* context) {
    CallbackState& state = *static_cast<CallbackState*>(context);
    state.executedValue = command.value;
    ++state.executionCount;
    state.executionThread = std::this_thread::get_id();
    switch (command.outcome) {
        case 1U: return DomainCommandResult::Rejected;
        case 2U: return DomainCommandResult::InvalidState;
        case 3U: return DomainCommandResult::OperationStarted;
        default: return DomainCommandResult::Completed;
    }
}

struct ActionFixture {
    NativeSynchronizer synchronizer;
    CallbackState callbacks;
    CommandPipelineConfig<TestCommand> config;
    CommandPipeline<TestCommand> pipeline;
    WebActionBridge<TestCommand, 1U> bridge;
    NativeActionBoundary<TestCommand, 1U> boundary;

    ActionFixture()
        : config(makeConfig(callbacks)), pipeline(config),
          bridge(synchronizer, pipeline), boundary(bridge) {}
private:
    static CommandPipelineConfig<TestCommand> makeConfig(CallbackState& state) {
        CommandPipelineConfig<TestCommand> value {};
        value.validator = validator;
        value.policy = policy;
        value.safety = safety;
        value.handler = commandHandler;
        value.handlerContext = &state;
        return value;
    }
};

void waitUntilWaiting(const NativeSynchronizer& synchronizer) {
    while (!synchronizer.waiting.load()) std::this_thread::yield();
}

void test_typed_command_is_owned_and_runs_in_application_context() {
    ActionFixture fixture;
    char source[] = "42";
    NativeActionResult result;
    const std::thread::id applicationThread = std::this_thread::get_id();
    std::thread http([&]() {
        const TestCommand parsed {
            static_cast<uint32_t>((source[0] - '0') * 10 + source[1] - '0'), 0U
        };
        result = fixture.boundary.submitAndWait(parsed, 500U);
    });
    waitUntilWaiting(fixture.synchronizer);
    source[0] = '9';
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    http.join();
    TEST_ASSERT_EQUAL_UINT32(42U, fixture.callbacks.executedValue);
    TEST_ASSERT_TRUE(fixture.callbacks.executionThread == applicationThread);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::Completed),
                            static_cast<uint8_t>(result.state));
    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_TRUE(result.outcomeKnown);
}

void test_completed_results_and_operation_started_are_preserved() {
    const CommandExecutionOutcome outcomes[] = {
        CommandExecutionOutcome::Handled, CommandExecutionOutcome::Handled,
        CommandExecutionOutcome::Handled, CommandExecutionOutcome::Handled,
        CommandExecutionOutcome::InvalidCommand,
        CommandExecutionOutcome::BlockedByPolicy,
        CommandExecutionOutcome::BlockedBySafety
    };
    for (uint8_t index = 0U; index < 7U; ++index) {
        ActionFixture fixture;
        NativeActionResult result;
        std::thread http([&]() {
            result = fixture.boundary.submitAndWait(TestCommand {1U, index}, 500U);
        });
        waitUntilWaiting(fixture.synchronizer);
        TEST_ASSERT_TRUE(fixture.bridge.processOne());
        http.join();
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::Completed),
                                static_cast<uint8_t>(result.state));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(outcomes[index]),
                                static_cast<uint8_t>(result.executionResult.outcome()));
        if (index == 3U) {
            TEST_ASSERT_EQUAL_UINT16(202U, result.candidateHttpStatus);
            TEST_ASSERT_EQUAL_UINT8(
                static_cast<uint8_t>(DomainCommandResult::OperationStarted),
                static_cast<uint8_t>(*result.executionResult.domainResult())
            );
        } else {
            TEST_ASSERT_EQUAL_UINT16(0U, result.candidateHttpStatus);
        }
    }

    NativeSynchronizer synchronizer;
    CommandPipelineConfig<TestCommand> invalidConfig {};
    CommandPipeline<TestCommand> invalidPipeline(invalidConfig);
    WebActionBridge<TestCommand, 1U> bridge(synchronizer, invalidPipeline);
    NativeActionBoundary<TestCommand, 1U> boundary(bridge);
    NativeActionResult invalidResult;
    std::thread http([&]() {
        invalidResult = boundary.submitAndWait(TestCommand {1U, 0U}, 500U);
    });
    waitUntilWaiting(synchronizer);
    TEST_ASSERT_TRUE(bridge.processOne());
    http.join();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandExecutionOutcome::InvalidPipeline),
        static_cast<uint8_t>(invalidResult.executionResult.outcome())
    );
    TEST_ASSERT_EQUAL_UINT16(0U, invalidResult.candidateHttpStatus);
}

void test_queue_full_and_submit_sync_failure_are_not_accepted() {
    ActionFixture fixture;
    ActionBridgeToken occupied;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ActionBridgeSubmitResult::Accepted),
        static_cast<uint8_t>(fixture.bridge.submit(TestCommand {1U, 0U}, occupied))
    );
    NativeActionResult result =
        fixture.boundary.submitAndWait(TestCommand {2U, 0U}, 0U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::NotAccepted),
                            static_cast<uint8_t>(result.state));
    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_EQUAL_UINT16(503U, result.candidateHttpStatus);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ActionBridgeAbandonResult::Abandoned),
        static_cast<uint8_t>(fixture.bridge.abandon(occupied))
    );
    TEST_ASSERT_TRUE(fixture.bridge.processOne());

    ActionFixture failed;
    failed.synchronizer.failLockCall.store(1U);
    result = failed.boundary.submitAndWait(TestCommand {3U, 0U}, 0U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::BridgeFailure),
                            static_cast<uint8_t>(result.state));
    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_EQUAL_UINT16(503U, result.candidateHttpStatus);
}

void test_timeout_is_accepted_unknown_and_reclaimed_later() {
    ActionFixture fixture;
    const NativeActionResult result =
        fixture.boundary.submitAndWait(TestCommand {7U, 0U}, 0U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NativeActionState::AcceptedOutcomeUnknown),
        static_cast<uint8_t>(result.state)
    );
    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_FALSE(result.outcomeKnown);
    TEST_ASSERT_TRUE(result.tokenProtocolClosed);
    TEST_ASSERT_EQUAL_UINT16(202U, result.candidateHttpStatus);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    TEST_ASSERT_EQUAL_UINT(1U, fixture.callbacks.executionCount);
    const NativeActionResult next =
        fixture.boundary.submitAndWait(TestCommand {8U, 0U}, 0U);
    TEST_ASSERT_TRUE(next.accepted);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    TEST_ASSERT_EQUAL_UINT(2U, fixture.callbacks.executionCount);
}

void test_wait_sync_failure_abandons_without_token_leak() {
    ActionFixture fixture;
    fixture.synchronizer.failLockCall.store(2U);
    const NativeActionResult result =
        fixture.boundary.submitAndWait(TestCommand {7U, 0U}, 10U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::BridgeFailure),
                            static_cast<uint8_t>(result.state));
    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_FALSE(result.outcomeKnown);
    TEST_ASSERT_TRUE(result.tokenProtocolClosed);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    const NativeActionResult next =
        fixture.boundary.submitAndWait(TestCommand {8U, 0U}, 0U);
    TEST_ASSERT_TRUE(next.accepted);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());

    ActionFixture persistentFailure;
    persistentFailure.synchronizer.failLockCall.store(2U);
    persistentFailure.synchronizer.secondFailLockCall.store(3U);
    persistentFailure.synchronizer.thirdFailLockCall.store(4U);
    const NativeActionResult unresolved =
        persistentFailure.boundary.submitAndWait(TestCommand {9U, 0U}, 10U);
    TEST_ASSERT_TRUE(unresolved.accepted);
    TEST_ASSERT_FALSE(unresolved.outcomeKnown);
    TEST_ASSERT_FALSE(unresolved.tokenProtocolClosed);
    ActionBridgeToken token;
    token.slotIndex = 0U;
    token.generation = 1U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ActionBridgeAbandonResult::Abandoned),
        static_cast<uint8_t>(persistentFailure.bridge.abandon(token))
    );
    TEST_ASSERT_TRUE(persistentFailure.bridge.processOne());
}

void detachAcceptedToken(void* context) {
    WebActionBridge<TestCommand, 1U>& bridge =
        *static_cast<WebActionBridge<TestCommand, 1U>*>(context);
    ActionBridgeToken token;
    token.slotIndex = 0U;
    token.generation = 1U;
    bridge.abandon(token);
}

void test_invalid_token_after_acceptance_preserves_acceptance() {
    ActionFixture fixture;
    fixture.synchronizer.waitHook = detachAcceptedToken;
    fixture.synchronizer.waitHookContext = &fixture.bridge;
    const NativeActionResult result =
        fixture.boundary.submitAndWait(TestCommand {7U, 0U}, 0U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeActionState::BridgeFailure),
                            static_cast<uint8_t>(result.state));
    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_FALSE(result.outcomeKnown);
    TEST_ASSERT_TRUE(result.tokenProtocolClosed);
    TEST_ASSERT_EQUAL_UINT16(202U, result.candidateHttpStatus);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
    const NativeActionResult next =
        fixture.boundary.submitAndWait(TestCommand {8U, 0U}, 0U);
    TEST_ASSERT_TRUE(next.accepted);
    TEST_ASSERT_TRUE(fixture.bridge.processOne());
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_route_body_policy_and_callback_view);
    RUN_TEST(test_zero_limit_is_bodyless_and_zero_view_is_null);
    RUN_TEST(test_product_routes_reservations_and_freeze);
    RUN_TEST(test_partial_product_registration_failure_blocks_begin);
    RUN_TEST(test_typed_command_is_owned_and_runs_in_application_context);
    RUN_TEST(test_completed_results_and_operation_started_are_preserved);
    RUN_TEST(test_queue_full_and_submit_sync_failure_are_not_accepted);
    RUN_TEST(test_timeout_is_accepted_unknown_and_reclaimed_later);
    RUN_TEST(test_wait_sync_failure_abandons_without_token_leak);
    RUN_TEST(test_invalid_token_after_acceptance_preserves_acceptance);
    return UNITY_END();
}

#endif
