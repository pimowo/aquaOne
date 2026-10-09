#include <unity.h>

#include "AquaCore/Web/HttpRouteRegistry.h"
#include "AquaCore/Web/HttpStreamingServerTransport.h"
#include "AquaCore/Web/NativeWebService.h"
#include "../../src/Web/HttpStreamingReceive.h"
#include "../../src/Web/HttpCoreReservedPaths.h"
#include "../../src/Web/HttpServerStopLifecycle.h"

using namespace AquaCore::Web;
using namespace AquaCore::Web::Internal;

namespace {

void normal(void*, const HttpRouteRequest&, WebResponseWriter&) {}

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t code, ContentType type) override {
        status = code;
        contentType = type;
        return true;
    }
    bool write(const char* data, size_t length) override {
        if (data == nullptr && length != 0U) return false;
        bodyLength += length;
        return true;
    }
    bool endResponse() override { ended = true; return true; }
    uint16_t status = 0U;
    ContentType contentType = ContentType::Html;
    size_t bodyLength = 0U;
    bool ended = false;
};

class Header final : public WebRequestContext {
public:
    bool hasHeader(const char* name) const override {
        return name != nullptr && name[0] == 'X';
    }
    size_t copyHeader(const char*, char* out, size_t capacity) const override {
        if (capacity > 3U) { out[0] = 'a'; out[1] = 'b'; out[2] = 'c'; out[3] = 0; }
        return 3U;
    }
    bool authenticateBasic(const char*, const char*) const override { return true; }
    bool requestBasicAuthentication(const char*) const override { return true; }
};

struct State {
    uint8_t body[4096U] {};
    size_t declared = 0U;
    size_t cursor = 0U;
    size_t readLimit = HTTP_STREAM_CHUNK_CAPACITY;
    int terminal = 0;
    bool terminalEnabled = false;
    size_t terminalAfter = 0U;
    int reads = 0;
    int oversize = 0;
    int start = 0;
    int data = 0;
    int end = 0;
    int abort = 0;
    size_t received = 0U;
    size_t chunks[32U] {};
    HttpStreamAbortReason reason = HttpStreamAbortReason::ReceiveError;
    bool accepting = true;
    bool stopAtStart = false;
    int stopAtData = 0;
    bool stopAtEnd = false;
    bool stopAtEndWithoutResponse = false;
    bool stopAfterRead = false;
    bool overreportRead = false;
    char copiedHeader[4U] {};
    bool headerValid = false;
    bool dataMatches = true;
    uint16_t responseStatus = 0U;
    bool responseEnded = false;
};

HttpStreamHandlerResult handler(void* context, const HttpStreamEvent& event,
                                WebResponseWriter& response) {
    State& s = *static_cast<State*>(context);
    switch (event.type) {
        case HttpStreamEventType::BodyStart:
            ++s.start;
            TEST_ASSERT_NOT_NULL(event.request);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HttpMethod::Post),
                                    static_cast<uint8_t>(event.request->method));
            TEST_ASSERT_EQUAL_STRING("/stream", event.request->path);
            TEST_ASSERT_EQUAL_UINT32(s.declared, event.request->contentLength);
            TEST_ASSERT_NOT_NULL(event.request->context);
            s.headerValid = event.request->context->hasHeader("X-Test") &&
                event.request->context->authenticateBasic("a", "b") &&
                event.request->context->copyHeader("X-Test", s.copiedHeader,
                                                   sizeof(s.copiedHeader)) == 3U;
            if (s.stopAtStart) {
                response.beginResponse(400U, ContentType::PlainText);
                response.endResponse();
                return HttpStreamHandlerResult::Stop;
            }
            break;
        case HttpStreamEventType::BodyData:
            TEST_ASSERT_NULL(event.request);
            TEST_ASSERT_NOT_NULL(event.data);
            TEST_ASSERT_TRUE(event.length > 0U && event.length <= 1024U);
            s.chunks[s.data] = event.length;
            ++s.data;
            for (size_t i = 0U; i < event.length; ++i) {
                if (event.data[i] != s.body[s.received + i]) s.dataMatches = false;
            }
            s.received += event.length;
            if (s.stopAtData == s.data) return HttpStreamHandlerResult::Stop;
            break;
        case HttpStreamEventType::BodyEnd:
            TEST_ASSERT_NULL(event.request);
            ++s.end;
            TEST_ASSERT_EQUAL_STRING("abc", s.copiedHeader);
            if (s.stopAtEndWithoutResponse) return HttpStreamHandlerResult::Stop;
            response.beginResponse(200U, ContentType::PlainText);
            response.writeText("ok");
            response.endResponse();
            if (s.stopAtEnd) return HttpStreamHandlerResult::Stop;
            break;
        case HttpStreamEventType::BodyAbort:
            TEST_ASSERT_NULL(event.request);
            ++s.abort;
            s.reason = event.abortReason;
            break;
    }
    return HttpStreamHandlerResult::Continue;
}

int reader(void* context, uint8_t* out, size_t capacity) {
    State& s = *static_cast<State*>(context);
    ++s.reads;
    if (s.terminalEnabled && s.cursor >= s.terminalAfter) return s.terminal;
    if (s.cursor == s.declared) return 0;
    if (s.overreportRead) return static_cast<int>(capacity + 1U);
    size_t n = s.declared - s.cursor;
    if (n > capacity) n = capacity;
    if (n > s.readLimit) n = s.readLimit;
    for (size_t i = 0U; i < n; ++i) out[i] = s.body[s.cursor + i];
    s.cursor += n;
    if (s.stopAfterRead) s.accepting = false;
    return static_cast<int>(n);
}

bool accepting(void* context) { return static_cast<State*>(context)->accepting; }
void oversize(void* context) { ++static_cast<State*>(context)->oversize; }

StreamReceiveResult run(State& s, size_t maximum) {
    uint8_t buffer[HTTP_STREAM_CHUNK_CAPACITY] {};
    Header header;
    Writer writer;
    const HttpStreamRequest request {HttpMethod::Post, "/stream", s.declared, &header};
    const StreamReceiveResult result = receiveStream(
        request, maximum, handler, &s, writer, buffer, reader, &s,
        accepting, &s, -3, oversize, &s
    );
    s.responseStatus = writer.status;
    s.responseEnded = writer.ended;
    if (result == StreamReceiveResult::Complete ||
        result == StreamReceiveResult::StoppedConsumed) {
        if (s.stopAtEndWithoutResponse) {
            TEST_ASSERT_FALSE(writer.ended);
            TEST_ASSERT_EQUAL_UINT16(0U, writer.status);
            TEST_ASSERT_EQUAL_UINT32(0U, writer.bodyLength);
        } else {
            TEST_ASSERT_TRUE(writer.ended);
            TEST_ASSERT_EQUAL_UINT16(200U, writer.status);
            TEST_ASSERT_EQUAL_UINT32(2U, writer.bodyLength);
        }
    }
    return result;
}

void fill(State& s, size_t length) {
    s.declared = length;
    for (size_t i = 0U; i < length && i < sizeof(s.body); ++i)
        s.body[i] = static_cast<uint8_t>((i * 197U) & 0xFFU);
    s.body[0] = 0x00U;
    s.body[1] = 0xFFU;
    s.body[2] = '\r';
    s.body[3] = '\n';
}

void test_registration_namespace() {
    HttpRouteRegistry r;
    TEST_ASSERT_TRUE(r.addRoute("/a", HttpMethod::Post, normal, nullptr));
    TEST_ASSERT_TRUE(r.addStreamingRoute("/b", HttpMethod::Post, handler, nullptr, 4096U));
    TEST_ASSERT_FALSE(r.addRoute("/a", HttpMethod::Post, normal, nullptr));
    TEST_ASSERT_FALSE(r.addStreamingRoute("/b", HttpMethod::Post, handler, nullptr, 1U));
    TEST_ASSERT_FALSE(r.addStreamingRoute("/a", HttpMethod::Post, handler, nullptr, 1U));
    TEST_ASSERT_FALSE(r.addRoute("/b", HttpMethod::Post, normal, nullptr));
    TEST_ASSERT_TRUE(r.addStreamingRoute("/a", HttpMethod::Get, handler, nullptr, 1U));
    TEST_ASSERT_EQUAL_UINT32(3U, r.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HttpRouteRegistry::RouteKind::Streaming),
                            static_cast<uint8_t>(r.routeAt(1U)->kind));
}

void test_capacity_and_freeze() {
    HttpRouteRegistry r;
    char path[] = "/aa";
    for (size_t i = 0U; i < 24U; ++i) {
        path[1] = static_cast<char>('a' + i);
        if ((i & 1U) == 0U)
            TEST_ASSERT_TRUE(r.addRoute(path, HttpMethod::Get, normal, nullptr));
        else
            TEST_ASSERT_TRUE(r.addStreamingRoute(path, HttpMethod::Post, handler, nullptr, 4096U));
    }
    TEST_ASSERT_EQUAL_UINT32(24U, r.size());
    for (size_t i = 0U; i < 24U; ++i) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>((i & 1U) == 0U
                ? HttpRouteRegistry::RouteKind::Normal
                : HttpRouteRegistry::RouteKind::Streaming),
            static_cast<uint8_t>(r.routeAt(i)->kind));
    }
    TEST_ASSERT_FALSE(r.addStreamingRoute("/extra", HttpMethod::Post, handler, nullptr, 1U));
    r.freeze();
    TEST_ASSERT_FALSE(r.addRoute("/late", HttpMethod::Get, normal, nullptr));
    TEST_ASSERT_FALSE(r.addStreamingRoute("/later", HttpMethod::Post, handler, nullptr, 1U));
    TEST_ASSERT_TRUE(r.isFrozen());
}

void test_chunks_and_binary() {
    State s;
    fill(s, 2500U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Complete),
                            static_cast<uint8_t>(run(s, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, s.start);
    TEST_ASSERT_EQUAL_UINT32(3U, s.data);
    TEST_ASSERT_EQUAL_UINT32(1U, s.end);
    TEST_ASSERT_EQUAL_UINT32(0U, s.abort);
    TEST_ASSERT_EQUAL_UINT32(1024U, s.chunks[0]);
    TEST_ASSERT_EQUAL_UINT32(1024U, s.chunks[1]);
    TEST_ASSERT_EQUAL_UINT32(452U, s.chunks[2]);
    TEST_ASSERT_EQUAL_UINT32(2500U, s.received);
    TEST_ASSERT_TRUE(s.dataMatches);
    TEST_ASSERT_TRUE(s.headerValid);
}

void test_partial_and_zero() {
    State s;
    fill(s, 2048U);
    s.readLimit = 137U;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Complete),
                            static_cast<uint8_t>(run(s, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(2048U, s.received);
    TEST_ASSERT_EQUAL_UINT32(1U, s.end);
    TEST_ASSERT_TRUE(s.dataMatches);
    State zero;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Complete),
                            static_cast<uint8_t>(run(zero, 0U)));
    TEST_ASSERT_EQUAL_UINT32(1U, zero.start);
    TEST_ASSERT_EQUAL_UINT32(0U, zero.data);
    TEST_ASSERT_EQUAL_UINT32(0U, zero.reads);
    TEST_ASSERT_EQUAL_UINT32(1U, zero.end);
}

void test_max_and_oversize() {
    State exact;
    fill(exact, 4096U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Complete),
                            static_cast<uint8_t>(run(exact, 4096U)));
    State plusOne;
    plusOne.declared = 4097U;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Oversize),
                            static_cast<uint8_t>(run(plusOne, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, plusOne.oversize);
    TEST_ASSERT_EQUAL_UINT32(0U, plusOne.reads);
    State tooBig;
    tooBig.declared = 1000000000U;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Oversize),
                            static_cast<uint8_t>(run(tooBig, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, tooBig.oversize);
    TEST_ASSERT_EQUAL_UINT32(0U, tooBig.reads);
    TEST_ASSERT_EQUAL_UINT32(0U, tooBig.start);
    TEST_ASSERT_EQUAL_UINT32(0U, tooBig.abort);
}

void test_handler_stop() {
    State start;
    fill(start, 2048U);
    start.stopAtStart = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::StoppedUnread),
                            static_cast<uint8_t>(run(start, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(0U, start.reads);
    TEST_ASSERT_EQUAL_UINT32(1U, start.abort);
    TEST_ASSERT_EQUAL_UINT16(400U, start.responseStatus);
    TEST_ASSERT_TRUE(start.responseEnded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HttpStreamAbortReason::HandlerStopped),
                            static_cast<uint8_t>(start.reason));
    State data;
    fill(data, 2500U);
    data.stopAtData = 2;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::StoppedUnread),
                            static_cast<uint8_t>(run(data, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(2U, data.reads);
    TEST_ASSERT_EQUAL_UINT32(0U, data.end);
    TEST_ASSERT_EQUAL_UINT32(1U, data.abort);
    State end;
    fill(end, 64U);
    end.stopAtEnd = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::StoppedConsumed),
                            static_cast<uint8_t>(run(end, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, end.end);
    TEST_ASSERT_EQUAL_UINT32(1U, end.abort);
}

void test_end_stop_without_response() {
    State s;
    fill(s, 64U);
    s.stopAtEndWithoutResponse = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::StoppedConsumed),
                            static_cast<uint8_t>(run(s, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, s.start);
    TEST_ASSERT_EQUAL_UINT32(1U, s.end);
    TEST_ASSERT_EQUAL_UINT32(1U, s.abort);
    TEST_ASSERT_EQUAL_UINT16(0U, s.responseStatus);
    TEST_ASSERT_FALSE(s.responseEnded);
}

void test_receive_failures_and_transport_stop() {
    const int values[] = {-3, 0, -2};
    const HttpStreamAbortReason reasons[] = {
        HttpStreamAbortReason::ReceiveTimeout,
        HttpStreamAbortReason::PeerClosed,
        HttpStreamAbortReason::ReceiveError
    };
    for (size_t i = 0U; i < 3U; ++i) {
        State s;
        fill(s, 2000U);
        s.readLimit = 1000U;
        // One positive read followed by the selected terminal result.
        s.terminal = values[i];
        s.terminalEnabled = true;
        s.terminalAfter = 1000U;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Aborted),
                                static_cast<uint8_t>(run(s, 4096U)));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(reasons[i]),
                                static_cast<uint8_t>(s.reason));
        TEST_ASSERT_EQUAL_UINT32(1U, s.abort);
        TEST_ASSERT_EQUAL_UINT32(0U, s.end);
    }
    State stopped;
    fill(stopped, 2000U);
    stopped.stopAfterRead = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Aborted),
                            static_cast<uint8_t>(run(stopped, 4096U)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HttpStreamAbortReason::TransportStopping),
                            static_cast<uint8_t>(stopped.reason));
    TEST_ASSERT_EQUAL_UINT32(1U, stopped.abort);
    TEST_ASSERT_EQUAL_UINT32(0U, stopped.end);
    State invalid;
    fill(invalid, 2000U);
    invalid.overreportRead = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamReceiveResult::Aborted),
                            static_cast<uint8_t>(run(invalid, 4096U)));
    TEST_ASSERT_EQUAL_UINT32(1U, invalid.reads);
    TEST_ASSERT_EQUAL_UINT32(1U, invalid.abort);
    TEST_ASSERT_EQUAL_UINT32(0U, invalid.end);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HttpStreamAbortReason::ReceiveError),
                            static_cast<uint8_t>(invalid.reason));
}

#if !defined(ARDUINO_ARCH_ESP32)
class Sync final : public SnapshotSynchronizer {
public:
    bool lock() override { return true; }
    void unlock() override {}
};

class CompositionTransport final : public HttpServerTransport,
                                   public HttpStreamingServerTransport {
public:
    using HttpServerTransport::addRoute;
    bool addRoute(const char* path, HttpMethod method, HttpRouteHandler callback,
                  void* context, const HttpRouteOptions& options) override {
        if (registrationFailed) return false;
        return registry.addRoute(path, method, callback, context, options);
    }
    bool addStreamingRoute(const char* path, HttpMethod method,
                           HttpStreamHandler callback, void* context,
                           size_t maximum) override {
        if (Internal::isCoreReadPath(path)) {
            registrationFailed = true;
            return false;
        }
        return registry.addStreamingRoute(path, method, callback, context, maximum);
    }
    bool setNotFoundHandler(HttpNotFoundHandler callback, void* context) override {
        return registry.setNotFoundHandler(callback, context);
    }
    bool begin(uint16_t) override {
        if (registrationFailed) return false;
        started = true;
        registry.freeze();
        return true;
    }
    void stop() override { started = false; }
    bool isRunning() const override { return started; }
    HttpRouteRegistry registry;
    bool started = false;
    bool registrationFailed = false;
};

void test_public_registration_stays_frozen_after_stop() {
    CompositionTransport transport;
    TEST_ASSERT_TRUE(transport.addRoute("/normal", HttpMethod::Get,
                                        normal, nullptr));
    TEST_ASSERT_TRUE(transport.addStreamingRoute("/stream", HttpMethod::Post,
                                                 handler, nullptr, 4096U));
    TEST_ASSERT_TRUE(transport.begin(80U));
    transport.stop();
    TEST_ASSERT_FALSE(transport.addRoute("/late", HttpMethod::Get,
                                         normal, nullptr));
    TEST_ASSERT_FALSE(transport.addStreamingRoute("/later", HttpMethod::Post,
                                                  handler, nullptr, 1U));
    TEST_ASSERT_EQUAL_UINT32(2U, transport.registry.size());
}

struct StopProbe {
    int calls = 0;
    bool succeeds = false;
};

bool stopProbe(void*, void* context) {
    StopProbe& probe = *static_cast<StopProbe*>(context);
    ++probe.calls;
    return probe.succeeds;
}

void test_failed_stop_retains_handle_until_retry_succeeds() {
    StopProbe probe;
    void* handle = &probe;
    uint16_t port = 80U;
    TEST_ASSERT_FALSE(stopRetainingHandle(handle, port, stopProbe, &probe));
    TEST_ASSERT_EQUAL_PTR(&probe, handle);
    TEST_ASSERT_EQUAL_UINT16(80U, port);
    TEST_ASSERT_FALSE(canStartNewServer(handle));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DestructionDisposition::MustFailSafe),
                            static_cast<uint8_t>(destructionDisposition(handle)));

    probe.succeeds = true;
    TEST_ASSERT_TRUE(stopRetainingHandle(handle, port, stopProbe, &probe));
    TEST_ASSERT_NULL(handle);
    TEST_ASSERT_EQUAL_UINT16(0U, port);
    TEST_ASSERT_TRUE(canStartNewServer(handle));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DestructionDisposition::Safe),
                            static_cast<uint8_t>(destructionDisposition(handle)));
    TEST_ASSERT_EQUAL_INT(2, probe.calls);
    TEST_ASSERT_TRUE(stopRetainingHandle(handle, port, stopProbe, &probe));
    TEST_ASSERT_EQUAL_INT(2, probe.calls);
}

void test_reserved_core_collision() {
    const char* paths[] = {"/", "/assets/aqua.css", "/api/system",
                           "/api/diagnostics"};
    for (size_t i = 0U; i < 4U; ++i) {
        for (size_t m = 0U; m < 2U; ++m) {
            CompositionTransport transport;
            TEST_ASSERT_FALSE(transport.addStreamingRoute(
                paths[i], m == 0U ? HttpMethod::Get : HttpMethod::Post,
                handler, nullptr, 0U));
            Sync sync;
            PublishedSnapshot<CoreSystemProjection> system(sync);
            PublishedSnapshot<CoreDiagnosticsProjection> diagnostics(sync);
            NativeWebService service(transport, system, diagnostics);
            WebConfig config;
            config.enabled = true;
            TEST_ASSERT_FALSE(service.begin(config));
            TEST_ASSERT_FALSE(transport.started);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NativeWebState::Failed),
                                    static_cast<uint8_t>(service.state()));
        }
    }
}
#endif

} // namespace

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "AquaCore/Web/EspIdfWebTransport.h"
volatile bool startTransportProof = false;
void setup() {
    UNITY_BEGIN();
    AquaCore::Web::EspIdfWebTransport transport;
    TEST_ASSERT_TRUE(transport.addStreamingRoute(
        "/stream", HttpMethod::Post, handler, nullptr, 4096U));
    if (startTransportProof) TEST_ASSERT_TRUE(transport.begin(80U));
    transport.stop();
    UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registration_namespace);
    RUN_TEST(test_capacity_and_freeze);
    RUN_TEST(test_chunks_and_binary);
    RUN_TEST(test_partial_and_zero);
    RUN_TEST(test_max_and_oversize);
    RUN_TEST(test_handler_stop);
    RUN_TEST(test_end_stop_without_response);
    RUN_TEST(test_receive_failures_and_transport_stop);
    RUN_TEST(test_reserved_core_collision);
    RUN_TEST(test_public_registration_stays_frozen_after_stop);
    RUN_TEST(test_failed_stop_retains_handle_until_retry_succeeds);
    return UNITY_END();
}
#endif
