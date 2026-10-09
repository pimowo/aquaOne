#include <unity.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include <AquaCore/Web/HttpStreamingServerTransport.h>
#include <AquaCore/Web/HttpBasicAuth.h>
#include <AquaCore/Web/SnapshotSynchronizer.h>

#include "../../src/DoserMultipartFirmwareParser.h"
#include "../../src/DoserNativeOtaRoute.h"

using namespace AquaCore::Web;

namespace {
class Sync final : public ActionBridgeSynchronizer, public SnapshotSynchronizer {
public:
    bool lock() override { return !failLock; }
    void unlock() override {}
    size_t completionSlotCapacity() const override { return 1U; }
    bool prepareCompletion(size_t) override { signaled = false; return true; }
    ActionBridgeWaitStatus waitForCompletion(size_t, uint32_t timeout) override {
        lastWait = timeout;
        if (onWait != nullptr) onWait(waitContext);
        return signaled ? ActionBridgeWaitStatus::Signaled : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t) override { signaled = true; return true; }
    void (*onWait)(void*) = nullptr;
    void* waitContext = nullptr;
    uint32_t lastWait = 0U;
    bool signaled = false;
    bool failLock = false;
};

class Authority final : public DoserOtaAuthority {
public:
    uint32_t nowMs() const override { return now; }
    size_t availableFirmwareSpace() const override { return capacity; }
    bool beginFirmwareUpdate(size_t size) override {
        order += 'B'; beginSize = size;
        if (onBegin != nullptr) onBegin(hookContext);
        return beginOk;
    }
    size_t writeFirmware(const uint8_t* data, size_t length) override {
        order += 'W'; written.insert(written.end(), data, data + length);
        if (onWrite != nullptr) onWrite(hookContext);
        return shortWrite ? length - 1U : longWrite ? length + 1U : length;
    }
    bool endFirmwareUpdate() override {
        order += 'E';
        if (onEnd != nullptr) onEnd(hookContext);
        return endOk;
    }
    void abortFirmwareUpdate() override { order += 'A'; ++aborts; }
    const char* firmwareError() const override { return "synthetic update error\n"; }
    void stopPumps() override { order += 'P'; }
    void setOtaInProgress(bool value) override { order += value ? 'T' : 'F'; guard = value; }
    uint32_t now = 0U;
    size_t capacity = 1310720U;
    size_t beginSize = 0U;
    bool beginOk = true, endOk = true, shortWrite = false, longWrite = false, guard = false;
    int aborts = 0;
    std::string order;
    std::vector<uint8_t> written;
    void (*onBegin)(void*) = nullptr;
    void (*onWrite)(void*) = nullptr;
    void (*onEnd)(void*) = nullptr;
    void* hookContext = nullptr;
};

class Restart final : public DoserOtaRestartScheduler {
public:
    bool isRestartPending() const override { return pending; }
    bool scheduleOtaRestart() override { ++calls; pending = true; return true; }
    bool pending = false;
    int calls = 0;
};

class Clock final : public DoserOtaClock {
public:
    uint32_t nowMs() const override { return now; }
    uint32_t now = 0U;
};

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t code, ContentType type) override {
        status = code; contentType = type; return true;
    }
    bool write(const char* data, size_t n) override { body.append(data, n); return true; }
    bool endResponse() override {
        if (failEnd) return false;
        ended = true; return true;
    }
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    std::string body;
    bool ended = false;
    bool failEnd = false;
};

class RequestContext final : public WebRequestContext {
public:
    bool hasHeader(const char* name) const override {
        return std::strcmp(name, "Content-Type") == 0 ? !type.empty() :
               std::strcmp(name, "X-Firmware-Size") == 0 ? !size.empty() : false;
    }
    size_t copyHeader(const char* name, char* out, size_t cap) const override {
        const std::string& value = std::strcmp(name, "Content-Type") == 0 ? type : size;
        if (cap > value.size()) std::memcpy(out, value.c_str(), value.size() + 1U);
        return value.size();
    }
    bool authenticateBasic(const char* user, const char* password) const override {
        return authorized && verifyHttpBasicAuthorization(
            authorization.c_str(), authorization.size(), user, password);
    }
    bool requestBasicAuthentication(const char*) const override {
        if (writer != nullptr) {
            writer->beginResponse(401U, ContentType::PlainText);
            writer->writeText("Unauthorized");
            writer->endResponse();
        }
        return true;
    }
    std::string type = "multipart/form-data; boundary=X";
    std::string size = "4";
    bool authorized = true;
    std::string authorization = "Basic c3ludGhldGljLXVzZXI6c3ludGhldGljLXBhc3N3b3Jk";
    Writer* writer = nullptr;
};

class Transport final : public HttpStreamingServerTransport {
public:
    bool addStreamingRoute(const char* path, HttpMethod method, HttpStreamHandler fn,
                           void* ctx, size_t max) override {
        if (std::strcmp(path, "/update") != 0 || method != HttpMethod::Post) return false;
        handler = fn; context = ctx; maximum = max; return true;
    }
    HttpStreamHandler handler = nullptr;
    void* context = nullptr;
    size_t maximum = 0U;
};

class Fixture {
public:
    explicit Fixture(uint64_t seed = 0U, const char* password = "synthetic-password")
        : bridge(sync, seed), capacity(sync), app(bridge, authority, restart, capacity),
          route(bridge, capacity, clock, "synthetic-user", password) {
        sync.onWait = process;
        sync.waitContext = this;
        TEST_ASSERT_TRUE(app.publishCapacity());
        TEST_ASSERT_TRUE(route.addTo(transport, static_cast<uint32_t>(authority.capacity)));
    }
    static void process(void* context) { static_cast<Fixture*>(context)->app.processOne(); }
    HttpStreamHandlerResult event(HttpStreamEvent& item) {
        return transport.handler(transport.context, item, writer);
    }
    HttpStreamHandlerResult start(const std::string& body) {
        requestContext.writer = &writer;
        const HttpStreamRequest request {HttpMethod::Post, "/update", body.size(), &requestContext};
        HttpStreamEvent eventItem(HttpStreamEventType::BodyStart);
        eventItem.request = &request;
        return event(eventItem);
    }
    HttpStreamHandlerResult data(const uint8_t* p, size_t n) {
        HttpStreamEvent item(HttpStreamEventType::BodyData);
        item.data = p; item.length = n;
        return event(item);
    }
    HttpStreamHandlerResult end() {
        HttpStreamEvent item(HttpStreamEventType::BodyEnd);
        return event(item);
    }
    Sync sync;
    StreamingUploadBridge bridge;
    DoserOtaCapacitySnapshot capacity;
    Authority authority;
    Restart restart;
    Clock clock;
    DoserOtaApplication app;
    DoserNativeOtaRoute route;
    Transport transport;
    RequestContext requestContext;
    Writer writer;
};

std::string body(const std::string& payload, const std::string& boundary = "X",
                 const std::string& filename = "firmware.bin") {
    return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"" +
           filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n" +
           payload + "\r\n--" + boundary + "--\r\n";
}

struct ParserCapture {
    std::string bytes;
    int starts = 0, ends = 0;
    static bool sink(void* context, DoserMultipartFirmwareParser::Event event,
                     const uint8_t* p, size_t n) {
        ParserCapture& self = *static_cast<ParserCapture*>(context);
        if (event == DoserMultipartFirmwareParser::Event::Start) ++self.starts;
        if (event == DoserMultipartFirmwareParser::Event::End) ++self.ends;
        if (event == DoserMultipartFirmwareParser::Event::Data)
            self.bytes.append(reinterpret_cast<const char*>(p), n);
        return true;
    }
};

void test_metadata_strict_limits() {
    char boundary[71] {};
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseContentType(
        "MuLtIpArT/FoRm-DaTa; boundary=abc", boundary));
    TEST_ASSERT_EQUAL_STRING("abc", boundary);
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseContentType(
        "multipart/form-data; boundary=\"a b\"", boundary));
    const char* invalid[] = {"", "text/plain; boundary=X", "multipart/form-data",
        "multipart/form-data; boundary=", "multipart/form-data; boundary=X; x=y",
        "multipart/form-data; boundary=X; boundary=Y",
        "multipart/form-data; boundary=\"X", "multipart/form-data; boundary=X ",
        "multipart/form-data; boundary=bad@x"};
    for (const char* value : invalid)
        TEST_ASSERT_FALSE(DoserMultipartFirmwareParser::parseContentType(value, boundary));
    const std::string seventy(70U, 'x'), seventyOne(71U, 'x');
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseContentType(
        ("multipart/form-data; boundary=" + seventy).c_str(), boundary));
    TEST_ASSERT_FALSE(DoserMultipartFirmwareParser::parseContentType(
        ("multipart/form-data; boundary=" + seventyOne).c_str(), boundary));
    uint32_t size = 0U;
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseFirmwareSize("0001", size));
    TEST_ASSERT_EQUAL_UINT32(1U, size);
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseFirmwareSize("4294967295", size));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, size);
    const char* badSize[] = {"", "0", "-1", "+1", " 1", "1 ", "1a", "4294967296", "00000000001"};
    for (const char* value : badSize)
        TEST_ASSERT_FALSE(DoserMultipartFirmwareParser::parseFirmwareSize(value, size));
    TEST_ASSERT_FALSE(DoserMultipartFirmwareParser::validContentLength(4U, 4U));
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::validContentLength(5U, 4U));
    TEST_ASSERT_FALSE(DoserMultipartFirmwareParser::validContentLength(669U, 4U));
}

void test_parser_every_split_and_binary() {
    const std::string payload = std::string("A\0\xff\r\n--X-?B", 11U);
    const std::string frame = body(payload);
    for (size_t split = 0U; split <= frame.size(); ++split) {
        DoserMultipartFirmwareParser parser;
        ParserCapture capture;
        TEST_ASSERT_TRUE(parser.begin("X"));
        TEST_ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(frame.data()), split,
                                     ParserCapture::sink, &capture));
        TEST_ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(frame.data() + split),
                                     frame.size() - split, ParserCapture::sink, &capture));
        TEST_ASSERT_TRUE(parser.finish(ParserCapture::sink, &capture));
        TEST_ASSERT_EQUAL_INT(1, capture.starts);
        TEST_ASSERT_EQUAL_INT(1, capture.ends);
        TEST_ASSERT_TRUE(capture.bytes == payload);
    }
}

void test_parser_emits_end_once() {
    const std::string frame = body("ABCD");
    DoserMultipartFirmwareParser parser;
    ParserCapture capture;
    TEST_ASSERT_TRUE(parser.begin("X"));
    TEST_ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(frame.data()),
                                 frame.size(), ParserCapture::sink, &capture));
    TEST_ASSERT_TRUE(parser.finish(ParserCapture::sink, &capture));
    TEST_ASSERT_FALSE(parser.finish(ParserCapture::sink, &capture));
    TEST_ASSERT_EQUAL_INT(1, capture.starts);
    TEST_ASSERT_EQUAL_INT(1, capture.ends);
    TEST_ASSERT_EQUAL_STRING("ABCD", capture.bytes.c_str());
}

void test_parser_one_byte_chunks_preserve_boundary_prefixes() {
    const std::string boundary(70U, 'Q');
    const std::string payload = std::string("A\0B\r\n--", 7U) + boundary.substr(0U, 69U) +
                                "X\r\n--" + boundary + "-Xtail";
    const std::string frame = body(payload, boundary);
    DoserMultipartFirmwareParser parser;
    ParserCapture capture;
    TEST_ASSERT_TRUE(parser.begin(boundary.c_str()));
    for (size_t i = 0U; i < frame.size(); ++i)
        TEST_ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(frame.data() + i),
                                     1U, ParserCapture::sink, &capture));
    TEST_ASSERT_TRUE(parser.finish(ParserCapture::sink, &capture));
    TEST_ASSERT_EQUAL_INT(1, capture.starts);
    TEST_ASSERT_EQUAL_INT(1, capture.ends);
    TEST_ASSERT_TRUE(capture.bytes == payload);
}

void test_parser_rejects_bad_structure() {
    const std::string valid = body("ABCD");
    const std::string invalid[] = {
        "preamble" + valid, valid + "epilogue",
        body("ABCD", "X", "../firmware.bin"),
        body("ABCD", "X", "firmware.txt"),
        body("ABCD", "X", std::string(125U, 'a') + ".bin"),
        "--X\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\n\nABCD\n--X--",
        "--X\r\nContent-Disposition: form-data; name=\"other\"; filename=\"a.bin\"\r\n\r\nABCD\r\n--X--",
        "--X\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\r\nX-Other: v\r\n\r\nABCD\r\n--X--",
        "--X\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\r\nContent-Type: multipart/mixed\r\n\r\nABCD\r\n--X--",
        "--X\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\r\n\r\nABCD\r\n--X\r\n",
        "--X\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\r\n\r\nABCD\r\n--X\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"b.bin\"\r\n\r\nQ\r\n--X--",
        valid.substr(0U, valid.size() - 6U)
    };
    for (const std::string& frame : invalid) {
        DoserMultipartFirmwareParser parser;
        ParserCapture capture;
        TEST_ASSERT_TRUE(parser.begin("X"));
        const bool fed = parser.feed(reinterpret_cast<const uint8_t*>(frame.data()),
                                     frame.size(), ParserCapture::sink, &capture);
        TEST_ASSERT_FALSE(fed && parser.finish(ParserCapture::sink, &capture));
    }
}

void test_parser_header_and_boundary_limits() {
    char boundary[71] {};
    TEST_ASSERT_TRUE(DoserMultipartFirmwareParser::parseContentType(
        "multipart/form-data; boundary=Z", boundary));
    const std::string reversed = "--Z\r\nContent-Disposition: form-data; filename=\"FIRMWARE.BIN\"; name=\"firmware\"\r\n\r\nX\r\n--Z--";
    DoserMultipartFirmwareParser parser;
    ParserCapture capture;
    TEST_ASSERT_TRUE(parser.begin("Z"));
    TEST_ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(reversed.data()),
                                 reversed.size(), ParserCapture::sink, &capture));
    TEST_ASSERT_TRUE(parser.finish(ParserCapture::sink, &capture));
    TEST_ASSERT_EQUAL_STRING("X", capture.bytes.c_str());
    const std::string disposition =
        "--Z\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.bin\"\r\n";
    const std::string type256 = "Content-Type: " + std::string(240U, 'x') + "\r\n";
    const std::string type257 = "Content-Type: " + std::string(241U, 'x') + "\r\n";
    DoserMultipartFirmwareParser maxLine;
    ParserCapture maxCapture;
    const std::string maxFrame = disposition + type256 + "\r\nX\r\n--Z--";
    TEST_ASSERT_TRUE(maxLine.begin("Z"));
    TEST_ASSERT_TRUE(maxLine.feed(reinterpret_cast<const uint8_t*>(maxFrame.data()),
                                  maxFrame.size(), ParserCapture::sink, &maxCapture));
    TEST_ASSERT_TRUE(maxLine.finish(ParserCapture::sink, &maxCapture));
    DoserMultipartFirmwareParser overLine;
    const std::string overFrame = disposition + type257 + "\r\nX\r\n--Z--";
    TEST_ASSERT_TRUE(overLine.begin("Z"));
    TEST_ASSERT_FALSE(overLine.feed(reinterpret_cast<const uint8_t*>(overFrame.data()),
                                    overFrame.size(), ParserCapture::sink, &capture));
    DoserMultipartFirmwareParser overBlock;
    const std::string huge = disposition + "Content-Type: " + std::string(500U, 'x') +
                             "\r\n\r\nX\r\n--Z--";
    TEST_ASSERT_TRUE(overBlock.begin("Z"));
    TEST_ASSERT_FALSE(overBlock.feed(reinterpret_cast<const uint8_t*>(huge.data()),
                                     huge.size(), ParserCapture::sink, &capture));
}

UploadWaitResult operation(Fixture& f, UploadOperation kind, uint64_t generation,
                           uint32_t expected = 0U, const uint8_t* bytes = nullptr,
                           size_t n = 0U, DoserOtaResult* output = nullptr) {
    UploadCommand command {};
    command.kind = kind; command.generation = generation;
    command.expectedSize = expected; command.length = static_cast<uint16_t>(n);
    TEST_ASSERT_TRUE(f.bridge.submit(command, bytes));
    DoserOtaResult result {};
    const UploadWaitResult waited = f.bridge.wait(generation, 30000U, result);
    if (output != nullptr) *output = result;
    return waited;
}

void test_application_lifecycle_order_and_restart() {
    Fixture f;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    DoserOtaResult result {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Completed),
        static_cast<int>(operation(f, UploadOperation::Start, generation, 4U, nullptr, 0U, &result)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_STRING("PTB", f.authority.order.c_str());
    TEST_ASSERT_TRUE(f.app.blocksMqttService());
    const uint8_t data[] = {0U, 255U, '\r', '\n'};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Completed),
        static_cast<int>(operation(f, UploadOperation::Chunk, generation, 0U, data, 4U, &result)));
    TEST_ASSERT_EQUAL_UINT32(4U, f.app.receivedBytes());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Completed),
        static_cast<int>(operation(f, UploadOperation::End, generation, 0U, nullptr, 0U, &result)));
    TEST_ASSERT_EQUAL_STRING("PTBWE", f.authority.order.c_str());
    TEST_ASSERT_TRUE(f.authority.guard);
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
    f.authority.now = 29999U; f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Completed),
        static_cast<int>(operation(f, UploadOperation::ArmRestart, generation, 0U, nullptr, 0U, &result)));
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
    f.authority.now = 50000U; f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
}

void test_application_failures_and_recovery() {
    Fixture f;
    uint64_t generation = 0U;
    f.restart.pending = true;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    DoserOtaResult result {};
    operation(f, UploadOperation::Start, generation, 4U, nullptr, 0U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::Busy), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(f.authority.order.empty());
    f.restart.pending = false;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    f.authority.capacity = 3U;
    operation(f, UploadOperation::Start, generation, 4U, nullptr, 0U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::TooLarge), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(f.authority.order.empty());
    f.authority.capacity = 100U; f.authority.beginOk = false;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 4U, nullptr, 0U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::UpdateFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_STRING("synthetic update error?", result.diagnostic);
    TEST_ASSERT_EQUAL_STRING("PTBF", f.authority.order.c_str());
    TEST_ASSERT_FALSE(f.authority.guard);
    f.authority.beginOk = true;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 4U);
    const uint8_t data[] = {1U, 2U, 3U, 4U};
    f.authority.shortWrite = true;
    operation(f, UploadOperation::Chunk, generation, 0U, data, 4U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::UpdateFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(1, f.authority.aborts);
    TEST_ASSERT_FALSE(f.authority.guard);
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    f.authority.shortWrite = false;
    operation(f, UploadOperation::Start, generation, 4U);
    operation(f, UploadOperation::Chunk, generation, 0U, data, 2U);
    operation(f, UploadOperation::End, generation, 0U, nullptr, 0U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::Invalid), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(2, f.authority.aborts);
}

void test_write_overreport_aborts_without_counting_bytes() {
    Fixture f;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 2U);
    f.authority.longWrite = true;
    const uint8_t bytes[] = {1U, 2U};
    DoserOtaResult result {};
    operation(f, UploadOperation::Chunk, generation, 0U, bytes, 2U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::UpdateFailure),
                          static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0U, f.app.receivedBytes());
    TEST_ASSERT_EQUAL_INT(1, f.authority.aborts);
    TEST_ASSERT_FALSE(f.authority.guard);
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
}

void test_bridge_owns_chunk_and_overflow_aborts() {
    Fixture f;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 2U);
    uint8_t bytes[] = {7U, 8U};
    UploadCommand command {};
    command.kind = UploadOperation::Chunk;
    command.generation = generation;
    command.length = 2U;
    TEST_ASSERT_TRUE(f.bridge.submit(command, bytes));
    bytes[0] = 99U;
    DoserOtaResult result {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Completed),
                          static_cast<int>(f.bridge.wait(generation, 30000U, result)));
    TEST_ASSERT_EQUAL_UINT(7U, f.authority.written[0]);
    TEST_ASSERT_EQUAL_UINT(8U, f.authority.written[1]);
    operation(f, UploadOperation::End, generation);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaApplication::State::SucceededAwaitingRestart),
                          static_cast<int>(f.app.state()));
    Fixture overflow;
    TEST_ASSERT_TRUE(overflow.bridge.beginSession(generation));
    operation(overflow, UploadOperation::Start, generation, 1U);
    const uint8_t tooMuch[] = {1U, 2U};
    operation(overflow, UploadOperation::Chunk, generation, 0U, tooMuch, 2U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::Invalid), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(overflow.authority.written.empty());
    TEST_ASSERT_EQUAL_INT(1, overflow.authority.aborts);
    TEST_ASSERT_FALSE(overflow.authority.guard);
}

void test_bridge_timeout_and_generation() {
    Fixture f;
    uint64_t first = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(first));
    f.sync.onWait = nullptr;
    UploadCommand command {};
    command.kind = UploadOperation::Start; command.generation = first; command.expectedSize = 4U;
    TEST_ASSERT_TRUE(f.bridge.submit(command));
    DoserOtaResult result {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::TimedOut),
                          static_cast<int>(f.bridge.wait(first, 30000U, result)));
    TEST_ASSERT_TRUE(f.bridge.cancellationRequested(first));
    uint64_t other = 0U;
    TEST_ASSERT_FALSE(f.bridge.beginSession(other));
    TEST_ASSERT_TRUE(f.app.processOne());
    TEST_ASSERT_TRUE(f.authority.order.empty());
    TEST_ASSERT_TRUE(f.bridge.beginSession(other));
    TEST_ASSERT_TRUE(other > first);
    TEST_ASSERT_FALSE(f.bridge.requestCancel(first));
    TEST_ASSERT_FALSE(f.bridge.submit(command));
    TEST_ASSERT_TRUE(f.bridge.requestCancel(other));
    TEST_ASSERT_TRUE(f.bridge.requestCancel(other));
    TEST_ASSERT_TRUE(f.app.processOne());
    Fixture exhausted(UINT64_MAX);
    TEST_ASSERT_FALSE(exhausted.bridge.beginSession(other));
}

struct TimeoutHook {
    TimeoutHook(Fixture* value, uint64_t token) : fixture(value), generation(token) {}
    Fixture* fixture;
    uint64_t generation;
    UploadWaitResult observed = UploadWaitResult::Unavailable;
    static void timeout(void* context) {
        TimeoutHook& self = *static_cast<TimeoutHook*>(context);
        self.fixture->sync.onWait = nullptr;
        DoserOtaResult ignored {};
        self.observed = self.fixture->bridge.wait(self.generation, 1U, ignored);
    }
};

void test_processing_chunk_timeout_cleans_after_write() {
    Fixture f;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 4U);
    TimeoutHook hook {&f, generation};
    f.authority.onWrite = TimeoutHook::timeout;
    f.authority.hookContext = &hook;
    const uint8_t data[] = {1U, 2U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Unavailable),
        static_cast<int>(operation(f, UploadOperation::Chunk, generation, 0U, data, 2U)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::TimedOut),
                          static_cast<int>(hook.observed));
    TEST_ASSERT_EQUAL_STRING("PTBWAF", f.authority.order.c_str());
    TEST_ASSERT_FALSE(f.authority.guard);
    TEST_ASSERT_FALSE(f.bridge.uploadActive());
    uint64_t next = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(next));
    TEST_ASSERT_TRUE(next > generation);
}

void test_processing_end_timeout_success_wins_and_watchdog() {
    Fixture f;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 2U);
    const uint8_t data[] = {1U, 2U};
    operation(f, UploadOperation::Chunk, generation, 0U, data, 2U);
    TimeoutHook hook {&f, generation};
    f.authority.onEnd = TimeoutHook::timeout;
    f.authority.hookContext = &hook;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::Unavailable),
        static_cast<int>(operation(f, UploadOperation::End, generation)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UploadWaitResult::TimedOut),
                          static_cast<int>(hook.observed));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaApplication::State::SucceededAwaitingRestart),
                          static_cast<int>(f.app.state()));
    TEST_ASSERT_TRUE(f.authority.guard);
    TEST_ASSERT_EQUAL_INT(0, f.authority.aborts);
    uint64_t next = 0U;
    TEST_ASSERT_FALSE(f.bridge.beginSession(next));
    f.authority.now = 30000U;
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
}

void test_watchdog_rollover_and_end_failure() {
    Fixture f;
    f.authority.now = UINT32_MAX - 100U;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.bridge.beginSession(generation));
    operation(f, UploadOperation::Start, generation, 1U);
    const uint8_t data[] = {1U};
    operation(f, UploadOperation::Chunk, generation, 0U, data, 1U);
    operation(f, UploadOperation::End, generation);
    f.authority.now = static_cast<uint32_t>(f.authority.now + 29999U);
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
    ++f.authority.now;
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
    Fixture failed;
    failed.authority.endOk = false;
    TEST_ASSERT_TRUE(failed.bridge.beginSession(generation));
    operation(failed, UploadOperation::Start, generation, 1U);
    operation(failed, UploadOperation::Chunk, generation, 0U, data, 1U);
    DoserOtaResult result {};
    operation(failed, UploadOperation::End, generation, 0U, nullptr, 0U, &result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaError::UpdateFailure),
                          static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(1, failed.authority.aborts);
    TEST_ASSERT_FALSE(failed.authority.guard);
    TEST_ASSERT_FALSE(failed.bridge.uploadActive());
}

void test_fixed_ownership_budget() {
    const size_t total = sizeof(StreamingUploadBridge) +
        sizeof(DoserOtaApplication) + sizeof(DoserNativeOtaRoute) +
        sizeof(DoserOtaCapacitySnapshot);
    std::printf("OTA sizeof: parser=%zu bridge=%zu application=%zu route=%zu capacity=%zu total=%zu\n",
        sizeof(DoserMultipartFirmwareParser), sizeof(StreamingUploadBridge),
        sizeof(DoserOtaApplication), sizeof(DoserNativeOtaRoute),
        sizeof(DoserOtaCapacitySnapshot), total);
    TEST_ASSERT_TRUE(total < 4096U);
    TEST_ASSERT_TRUE(sizeof(StreamingUploadBridge) >= 1024U);
    TEST_ASSERT_TRUE(sizeof(DoserMultipartFirmwareParser) < 1200U);
}

void test_route_valid_and_auth() {
    Fixture f;
    const std::string frame = body("ABCD");
    f.requestContext.authorized = false;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_UINT(401U, f.writer.status);
    TEST_ASSERT_TRUE(f.authority.order.empty());
    f.requestContext.authorized = true;
    f.writer = Writer();
    f.requestContext.authorization = "Basic c3ludGhldGljLXVzZXI6d3Jvbmc=";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_UINT(401U, f.writer.status);
    TEST_ASSERT_TRUE(f.authority.order.empty());
    f.writer = Writer();
    f.requestContext.authorization = "Basic c3ludGhldGljLXVzZXI6c3ludGhldGljLXBhc3N3b3Jk";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    for (size_t i = 0U; i < frame.size(); i += 7U) {
        const size_t n = frame.size() - i < 7U ? frame.size() - i : 7U;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
            static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data() + i), n)));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.end()));
    TEST_ASSERT_EQUAL_UINT(200U, f.writer.status);
    TEST_ASSERT_EQUAL_STRING("Aktualizacja zakończona. Urządzenie uruchomi się ponownie.",
                             f.writer.body.c_str());
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
    TEST_ASSERT_EQUAL_STRING("PTBWE", f.authority.order.c_str());
    TEST_ASSERT_EQUAL_INT(4, static_cast<int>(f.authority.written.size()));
    TEST_ASSERT_TRUE(f.authority.guard);
}

void test_route_streams_multiple_firmware_chunks() {
    Fixture f;
    const std::string payload(2300U, 'A');
    const std::string frame = body(payload);
    f.requestContext.size = "2300";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    for (size_t i = 0U; i < frame.size(); i += 257U) {
        const size_t n = frame.size() - i < 257U ? frame.size() - i : 257U;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
            static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data() + i), n)));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.end()));
    TEST_ASSERT_EQUAL_UINT(200U, f.writer.status);
    TEST_ASSERT_EQUAL_UINT(2300U, f.authority.written.size());
    TEST_ASSERT_TRUE(std::equal(f.authority.written.begin(), f.authority.written.end(),
                                payload.begin()));
    TEST_ASSERT_TRUE(std::count(f.authority.order.begin(), f.authority.order.end(), 'W') >= 3);
}

void test_route_malformed_before_and_after_start() {
    Fixture before;
    const std::string bad = body("ABCD", "X", "bad.txt");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(before.start(bad)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
        static_cast<int>(before.data(reinterpret_cast<const uint8_t*>(bad.data()), bad.size())));
    TEST_ASSERT_EQUAL_UINT(400U, before.writer.status);
    TEST_ASSERT_TRUE(before.authority.order.empty());
    Fixture after;
    std::string malformed = body("ABCD");
    malformed[malformed.size() - 3U] = 'X';
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(after.start(malformed)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
        static_cast<int>(after.data(reinterpret_cast<const uint8_t*>(malformed.data()), malformed.size())));
    TEST_ASSERT_EQUAL_UINT(400U, after.writer.status);
    TEST_ASSERT_TRUE(after.bridge.cancellationRequested(after.bridge.activeGeneration()));
    TEST_ASSERT_TRUE(after.app.processOne());
    TEST_ASSERT_EQUAL_INT(1, after.authority.aborts);
    TEST_ASSERT_FALSE(after.authority.guard);
}

void test_route_metadata_limits_and_receive_abort() {
    Fixture f;
    const std::string frame = body("ABCD");
    f.requestContext.type = std::string(128U, 'x');
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_UINT(400U, f.writer.status);
    f.requestContext.type = "multipart/form-data; boundary=X";
    f.requestContext.size = "9999999";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_UINT(413U, f.writer.status);
    f.requestContext.size = "4"; f.writer = Writer();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    const size_t headersEnd = frame.find("\r\n\r\n") + 4U;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
        static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data()), headersEnd)));
    HttpStreamEvent abort(HttpStreamEventType::BodyAbort);
    abort.abortReason = HttpStreamAbortReason::ReceiveTimeout;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.event(abort)));
    TEST_ASSERT_EQUAL_UINT(408U, f.writer.status);
    TEST_ASSERT_TRUE(f.app.processOne());
    TEST_ASSERT_EQUAL_INT(1, f.authority.aborts);
}

void test_route_disabled_busy_and_request_deadline() {
    const std::string frame = body("ABCD");
    const char* disabledPasswords[] = {nullptr, "", "CHANGE_ME_BEFORE_USE"};
    for (const char* password : disabledPasswords) {
        Fixture disabled(0U, password);
        disabled.requestContext.authorized = false;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                              static_cast<int>(disabled.start(frame)));
        TEST_ASSERT_EQUAL_UINT(503U, disabled.writer.status);
        TEST_ASSERT_TRUE(disabled.authority.order.empty());
    }
    Fixture busy;
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(busy.bridge.beginSession(generation));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(busy.start(frame)));
    TEST_ASSERT_EQUAL_UINT(409U, busy.writer.status);
    TEST_ASSERT_TRUE(busy.bridge.uploadActive());
    TEST_ASSERT_TRUE(busy.bridge.releaseSession(generation));
    Fixture deadline;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(deadline.start(frame)));
    deadline.clock.now = 600000U;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
        static_cast<int>(deadline.data(reinterpret_cast<const uint8_t*>(frame.data()), 1U)));
    TEST_ASSERT_EQUAL_UINT(408U, deadline.writer.status);
    TEST_ASSERT_TRUE(deadline.authority.order.empty());
    Fixture unavailable;
    unavailable.sync.failLock = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(unavailable.start(frame)));
    TEST_ASSERT_EQUAL_UINT(503U, unavailable.writer.status);
}

void test_route_end_outcome_unknown_and_abort_reasons() {
    const std::string frame = body("ABCD");
    Fixture f;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
        static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data()), frame.size())));
    f.sync.onWait = nullptr; // END remains queued when its waiter times out.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.end()));
    TEST_ASSERT_EQUAL_UINT(503U, f.writer.status);
    TEST_ASSERT_TRUE(f.writer.body.find("outcome unknown") != std::string::npos);
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
    HttpStreamEvent abort(HttpStreamEventType::BodyAbort);
    abort.abortReason = HttpStreamAbortReason::HandlerStopped;
    f.event(abort);
    TEST_ASSERT_TRUE(f.app.processOne());
    TEST_ASSERT_EQUAL_INT(1, f.authority.aborts);
    TEST_ASSERT_FALSE(f.authority.guard);
    const HttpStreamAbortReason reasons[] = {
        HttpStreamAbortReason::PeerClosed, HttpStreamAbortReason::ReceiveError,
        HttpStreamAbortReason::TransportStopping, HttpStreamAbortReason::HandlerStopped
    };
    for (HttpStreamAbortReason reason : reasons) {
        Fixture aborted;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                              static_cast<int>(aborted.start(frame)));
        const size_t headerEnd = frame.find("\r\n\r\n") + 4U;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
            static_cast<int>(aborted.data(reinterpret_cast<const uint8_t*>(frame.data()), headerEnd)));
        HttpStreamEvent eventItem(HttpStreamEventType::BodyAbort);
        eventItem.abortReason = reason;
        aborted.event(eventItem);
        TEST_ASSERT_TRUE(aborted.app.processOne());
        TEST_ASSERT_EQUAL_INT(1, aborted.authority.aborts);
        TEST_ASSERT_FALSE(aborted.authority.guard);
        TEST_ASSERT_EQUAL_INT(0, aborted.restart.calls);
    }
}

void test_committed_response_failure_uses_watchdog() {
    Fixture f;
    const std::string frame = body("ABCD");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
        static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data()), frame.size())));
    f.writer.failEnd = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.end()));
    TEST_ASSERT_EQUAL_UINT(200U, f.writer.status);
    TEST_ASSERT_FALSE(f.writer.ended);
    TEST_ASSERT_EQUAL_INT(0, f.restart.calls);
    TEST_ASSERT_TRUE(f.authority.guard);
    f.authority.now = 30000U;
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
}

void test_route_processing_end_timeout_is_not_success_response() {
    Fixture f;
    const std::string frame = body("ABCD");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
                          static_cast<int>(f.start(frame)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Continue),
        static_cast<int>(f.data(reinterpret_cast<const uint8_t*>(frame.data()), frame.size())));
    const uint64_t generation = f.bridge.activeGeneration();
    TimeoutHook hook(&f, generation);
    f.authority.onEnd = TimeoutHook::timeout;
    f.authority.hookContext = &hook;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HttpStreamHandlerResult::Stop),
                          static_cast<int>(f.end()));
    TEST_ASSERT_EQUAL_UINT(503U, f.writer.status);
    TEST_ASSERT_TRUE(f.writer.body.find("outcome unknown") != std::string::npos);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DoserOtaApplication::State::SucceededAwaitingRestart),
                          static_cast<int>(f.app.state()));
    TEST_ASSERT_EQUAL_INT(0, f.authority.aborts);
    HttpStreamEvent abort(HttpStreamEventType::BodyAbort);
    abort.abortReason = HttpStreamAbortReason::HandlerStopped;
    f.event(abort);
    f.authority.now = 30000U;
    f.app.serviceRestartWatchdog();
    TEST_ASSERT_EQUAL_INT(1, f.restart.calls);
}
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_metadata_strict_limits);
    RUN_TEST(test_parser_every_split_and_binary);
    RUN_TEST(test_parser_emits_end_once);
    RUN_TEST(test_parser_one_byte_chunks_preserve_boundary_prefixes);
    RUN_TEST(test_parser_rejects_bad_structure);
    RUN_TEST(test_parser_header_and_boundary_limits);
    RUN_TEST(test_application_lifecycle_order_and_restart);
    RUN_TEST(test_application_failures_and_recovery);
    RUN_TEST(test_write_overreport_aborts_without_counting_bytes);
    RUN_TEST(test_bridge_owns_chunk_and_overflow_aborts);
    RUN_TEST(test_bridge_timeout_and_generation);
    RUN_TEST(test_processing_chunk_timeout_cleans_after_write);
    RUN_TEST(test_processing_end_timeout_success_wins_and_watchdog);
    RUN_TEST(test_watchdog_rollover_and_end_failure);
    RUN_TEST(test_fixed_ownership_budget);
    RUN_TEST(test_route_valid_and_auth);
    RUN_TEST(test_route_streams_multiple_firmware_chunks);
    RUN_TEST(test_route_malformed_before_and_after_start);
    RUN_TEST(test_route_metadata_limits_and_receive_abort);
    RUN_TEST(test_route_disabled_busy_and_request_deadline);
    RUN_TEST(test_route_end_outcome_unknown_and_abort_reasons);
    RUN_TEST(test_committed_response_failure_uses_watchdog);
    RUN_TEST(test_route_processing_end_timeout_is_not_success_response);
    return UNITY_END();
}
