#include <unity.h>

#include <cstring>
#include <string>

#include <AquaCore/Web/HttpBasicAuth.h>
#include <AquaCore/Web/CoreWebProjectionSources.h>
#include <AquaCore/Web/CoreWebProjectionPublisher.h>
#include <AquaCore/Web/HttpRouteRegistry.h>

#include "../../src/DoserNativeWeb.h"
#include "../../src/DoserDiagnosticsProjection.h"

using namespace AquaCore;
using namespace AquaCore::Web;

namespace {
bool hasJsonIntegerMember(const std::string& object, const char* key, int expected) {
    const std::string quotedKey = std::string("\"") + key + "\"";
    const size_t keyPosition = object.find(quotedKey);
    if (keyPosition == std::string::npos) return false;
    const size_t colon = object.find(':', keyPosition + quotedKey.size());
    if (colon == std::string::npos) return false;
    size_t valuePosition = colon + 1U;
    while (valuePosition < object.size() &&
           (object[valuePosition] == ' ' || object[valuePosition] == '\t' ||
            object[valuePosition] == '\r' || object[valuePosition] == '\n')) ++valuePosition;
    bool negative = false;
    if (valuePosition < object.size() && object[valuePosition] == '-') {
        negative = true; ++valuePosition;
    }
    if (valuePosition >= object.size() || object[valuePosition] < '0' ||
        object[valuePosition] > '9') return false;
    int value = 0;
    do { value = value * 10 + (object[valuePosition] - '0'); ++valuePosition; }
    while (valuePosition < object.size() && object[valuePosition] >= '0' &&
           object[valuePosition] <= '9');
    if (valuePosition < object.size() && object[valuePosition] != ',' &&
        object[valuePosition] != '}') return false;
    return (negative ? -value : value) == expected;
}

bool hasApiProtocolVersion(const char* json, int major, int minor) {
    if (json == nullptr) return false;
    const std::string document(json);
    const size_t key = document.find("\"api_protocol_version\"");
    if (key == std::string::npos) return false;
    size_t open = document.find(':', key + std::strlen("\"api_protocol_version\""));
    if (open == std::string::npos) return false;
    ++open;
    while (open < document.size() &&
           (document[open] == ' ' || document[open] == '\t' ||
            document[open] == '\r' || document[open] == '\n')) ++open;
    if (open >= document.size() || document[open] != '{') return false;
    size_t close = open + 1U;
    unsigned int depth = 1U;
    for (; close < document.size() && depth != 0U; ++close) {
        if (document[close] == '{') ++depth;
        else if (document[close] == '}') --depth;
    }
    if (depth != 0U) return false;
    const std::string versionObject = document.substr(open, close - open);
    return hasJsonIntegerMember(versionObject, "major", major) &&
           hasJsonIntegerMember(versionObject, "minor", minor);
}

class Lock final : public SnapshotSynchronizer, public ActionBridgeSynchronizer {
public:
    bool lock() override {
        if (failNextLock) { failNextLock = false; return false; }
        return true;
    }
    void unlock() override {}
    size_t completionSlotCapacity() const override { return 4U; }
    bool prepareCompletion(size_t slot) override {
        signaled[slot] = false; return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t timeout) override {
        lastTimeout = timeout;
        if (failWait) return ActionBridgeWaitStatus::Failure;
        if (onWait != nullptr) onWait(waitContext);
        return signaled[slot] ? ActionBridgeWaitStatus::Signaled
                              : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override {
        signaled[slot] = true; return true;
    }
    bool signaled[4U] {};
    bool failNextLock = false;
    bool failWait = false;
    uint32_t lastTimeout = 0U;
    void (*onWait)(void*) = nullptr;
    void* waitContext = nullptr;
};

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t code, ContentType type) override {
        status = code; contentType = type; return true;
    }
    bool write(const char* text, size_t length) override {
        body.append(text, length); return true;
    }
    bool endResponse() override { ended = true; return true; }
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    std::string body;
    bool ended = false;
};

class Context final : public WebRequestContext {
public:
    bool hasHeader(const char* name) const override {
        return authorization != nullptr && std::strcmp(name, "Authorization") == 0;
    }
    size_t copyHeader(const char* name, char* output, size_t capacity) const override {
        if (output != nullptr && capacity != 0U) output[0] = '\0';
        if (!hasHeader(name)) return 0U;
        const size_t length = std::strlen(authorization);
        if (output != nullptr && capacity > length)
            std::memcpy(output, authorization, length + 1U);
        return length;
    }
    bool authenticateBasic(const char* user, const char* password) const override {
        return authorization != nullptr && verifyHttpBasicAuthorization(
            authorization, std::strlen(authorization), user, password);
    }
    bool requestBasicAuthentication(const char* realm) const override {
        if (!validHttpBasicRealm(realm) || writer == nullptr) return false;
        challenged = true;
        challenge = std::string("Basic realm=\"") + realm + "\"";
        writer->beginResponse(401U, ContentType::PlainText);
        writer->writeText("Unauthorized");
        return writer->endResponse();
    }
    const char* authorization = nullptr;
    mutable Writer* writer = nullptr;
    mutable bool challenged = false;
    mutable std::string challenge;
};

class Transport final : public HttpServerTransport {
public:
    bool addRoute(const char* path, HttpMethod method, HttpRouteHandler handler,
                  void* context, const HttpRouteOptions& options) override {
        return !failRegistration && routes.addRoute(path, method, handler, context, options);
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) override {
        return routes.setNotFoundHandler(handler, context);
    }
    bool begin(uint16_t) override { running = !failBegin; return running; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }
    Writer request(const char* path, HttpMethod method, Context& context,
                   size_t bodyLength = 0U) {
        Writer output;
        context.writer = &output;
        for (size_t i = 0U; i < routes.size(); ++i) {
            const HttpRouteRegistry::Route* route = routes.routeAt(i);
            if (route->method != method || std::strcmp(route->path, path) != 0) continue;
            if (bodyLength > route->options.maxBodyLength) {
                output.beginResponse(413U, ContentType::PlainText);
                output.endResponse();
                return output;
            }
            const HttpRouteRequest view {method, path, nullptr, bodyLength, &context};
            route->handler(route->context, view, output);
            return output;
        }
        if (routes.notFoundHandler())
            routes.notFoundHandler()(routes.notFoundContext(), output);
        return output;
    }
    HttpRouteRegistry routes;
    bool failRegistration = false;
    bool failBegin = false;
    bool running = false;
};

class Backend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override { return uptime; }
    RestartReason restartReason() const override { return RestartReason::PowerOn; }
    uint32_t uptime = 100U;
};

class Facts final : public DoserDiagnosticsFactsSource {
public:
    bool read(DoserDiagnosticsFacts& out) const override {
        out = facts; return true;
    }
    DoserDiagnosticsFacts facts {};
};

class Authority final : public DoserRestartAuthority {
public:
    uint32_t nowMs() const override { return now; }
    void stopPumps() override { order += 'P'; }
    void restartDevice() override { order += 'R'; }
    uint32_t now = 0U;
    std::string order;
};

class Fixture {
public:
    explicit Fixture(const char* password = "test-password")
        : system(backend), systemSource(system, canonicalIdentity), diagnosticSource(facts),
          systemSnapshot(lock), diagnosticSnapshot(lock),
          publisher(systemSource, &diagnosticSource, systemSnapshot,
                    diagnosticSnapshot), service(transport, systemSnapshot,
                    diagnosticSnapshot), bridge(lock), upload(lock), app(bridge, authority),
          doserRoutes(bridge, "test-user", password, &upload) {
        app.setOtaSessionView(&upload);
        TEST_ASSERT_TRUE(system.begin(DeviceIdentity("Doser", "Test", "1", "S3")));
        const uint8_t bytes[] {0x24U, 0x6FU, 0x28U, 0xA1U, 0xB2U, 0xC3U};
        Identity::DeviceId id;
        TEST_ASSERT_TRUE(id.assign(bytes, sizeof(bytes)).isValid());
        TEST_ASSERT_TRUE(canonicalIdentity.assign("doser", id).isValid());
        facts.facts.systemReady = true;
        facts.facts.timeEnabled = true;
        facts.facts.timeValid = true;
        facts.facts.ntpConfigured = true;
        facts.facts.ntpSynced = true;
        facts.facts.connected = true;
        facts.facts.networkState = Network::NetworkState::Connected;
        facts.facts.productHealth = Diagnostics::HealthState::Ok;
        TEST_ASSERT_TRUE(doserRoutes.addTo(service));
    }
    bool begin(bool publish = true) {
        if (publish) {
            CoreWebPublicationResult result = publisher.update();
            TEST_ASSERT_TRUE(result.systemPublished);
            TEST_ASSERT_TRUE(result.diagnosticsPublished);
        }
        WebConfig config {}; config.enabled = true;
        return service.begin(config);
    }
    Backend backend;
    SystemService system;
    Identity::DeviceIdentity canonicalIdentity;
    SystemServiceWebProjectionSource systemSource;
    Facts facts;
    DoserDiagnosticsProjectionSource diagnosticSource;
    Lock lock;
    PublishedSnapshot<CoreSystemProjection> systemSnapshot;
    PublishedSnapshot<CoreDiagnosticsProjection> diagnosticSnapshot;
    CoreWebProjectionPublisher publisher;
    Transport transport;
    NativeWebService service;
    DoserWebBridge bridge;
    StreamingUploadBridge upload;
    Authority authority;
    DoserWebApplication app;
    DoserNativeWebRoutes doserRoutes;
};

void processOne(void* context) {
    static_cast<DoserWebApplication*>(context)->processOne();
}

void test_inventory_and_read_routes() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    TEST_ASSERT_EQUAL_UINT(6U, f.transport.routes.size());
    Context context;
    TEST_ASSERT_EQUAL_UINT(200U, f.transport.request("/", HttpMethod::Get, context).status);
    TEST_ASSERT_EQUAL_UINT(200U, f.transport.request("/assets/aqua.css", HttpMethod::Get, context).status);
    Writer systemApi = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, systemApi.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"deviceType\":\"Doser\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"device_type\":\"doser\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"device_id\":\"246F28A1B2C3\""));
    TEST_ASSERT_TRUE(hasApiProtocolVersion(systemApi.body.c_str(), 1, 0));
    Writer diagnostics = f.transport.request("/api/diagnostics", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, diagnostics.status);
    TEST_ASSERT_TRUE(diagnostics.body.find("{\"health\":\"unknown\"") != std::string::npos);
    TEST_ASSERT_TRUE(diagnostics.body.find("\"storage\":{\"health\":\"unknown\"") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(404U, f.transport.request("/update", HttpMethod::Post, context).status);
    TEST_ASSERT_EQUAL_UINT(404U, f.transport.request("/api/status", HttpMethod::Get, context).status);
    TEST_ASSERT_EQUAL_UINT(404U, f.transport.request("/missing", HttpMethod::Get, context).status);
}

void test_unpublished_and_snapshot_isolation() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin(false));
    Context context;
    TEST_ASSERT_EQUAL_UINT(503U, f.transport.request("/api/diagnostics", HttpMethod::Get, context).status);
    TEST_ASSERT_EQUAL_UINT(503U, f.transport.request("/api/system", HttpMethod::Get, context).status);
    TEST_ASSERT_TRUE(f.publisher.update().systemPublished);
    Writer old = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_TRUE(old.body.find("\"uptimeMs\":100") != std::string::npos);
    f.backend.uptime = 200U;
    Writer stillOld = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_TRUE(stillOld.body.find("\"uptimeMs\":100") != std::string::npos);
    TEST_ASSERT_TRUE(f.publisher.update().systemPublished);
    Writer fresh = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_TRUE(fresh.body.find("\"uptimeMs\":200") != std::string::npos);
}

void test_diagnostics_source_preserves_unknown_storage_and_real_time() {
    Facts facts;
    facts.facts.systemReady = true;
    facts.facts.timeEnabled = true;
    facts.facts.timeValid = true;
    facts.facts.rtcConfigured = false; // Deliberate NTP-only prototype.
    facts.facts.ntpConfigured = true;
    facts.facts.ntpSynced = true;
    facts.facts.connected = true;
    facts.facts.networkState = Network::NetworkState::Connected;
    facts.facts.productHealth = Diagnostics::HealthState::Warning;
    std::strncpy(facts.facts.ssid, "synthetic-test-ssid",
                 sizeof(facts.facts.ssid) - 1U);
    DoserDiagnosticsProjectionSource source(facts);
    CoreDiagnosticsProjection value {};
    TEST_ASSERT_TRUE(source.read(value));
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(Diagnostics::HealthState::Ok),
                           static_cast<unsigned>(value.value.timeHealth));
    TEST_ASSERT_FALSE(value.value.time.rtcReady);
    TEST_ASSERT_FALSE(value.value.time.rtcValid);
    TEST_ASSERT_TRUE(value.value.time.ntpAvailable);
    TEST_ASSERT_TRUE(value.value.time.ntpInitialized);
    TEST_ASSERT_EQUAL_STRING("NTP", value.value.time.providerName);
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(Diagnostics::HealthState::Unknown),
                           static_cast<unsigned>(value.value.storageHealth));
    TEST_ASSERT_FALSE(value.value.storage.backendReady);
    TEST_ASSERT_EQUAL_STRING("synthetic-test-ssid", value.value.network.ssid);
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(Diagnostics::HealthState::Warning),
                           static_cast<unsigned>(value.value.overallHealth));
    facts.facts.connected = false;
    facts.facts.networkState = Network::NetworkState::Disabled;
    TEST_ASSERT_TRUE(source.read(value));
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(Diagnostics::HealthState::Ok),
                           static_cast<unsigned>(value.value.networkHealth));
}

void test_disabled_and_failed_auth_do_not_submit() {
    const char* disabled[] = {nullptr, "", "CHANGE_ME_BEFORE_USE"};
    for (size_t i = 0U; i < 3U; ++i) {
        Fixture f(disabled[i]);
        TEST_ASSERT_TRUE(f.begin());
        Context context;
        Writer response = f.transport.request("/api/restart", HttpMethod::Post, context);
        TEST_ASSERT_EQUAL_UINT(503U, response.status);
        TEST_ASSERT_EQUAL_UINT(503U,
            f.transport.request("/update", HttpMethod::Get, context).status);
        TEST_ASSERT_FALSE(context.challenged);
        TEST_ASSERT_EQUAL_UINT(0U, f.bridge.pendingCount());
        TEST_ASSERT_FALSE(f.app.restartPending());
    }
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    Context context;
    Writer missing = f.transport.request("/api/restart", HttpMethod::Post, context);
    TEST_ASSERT_EQUAL_UINT(401U, missing.status);
    TEST_ASSERT_EQUAL_UINT(401U,
        f.transport.request("/update", HttpMethod::Get, context).status);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"PMW AquaDoser\"", context.challenge.c_str());
    context.authorization = "Basic dGVzdC11c2VyOndyb25n";
    Writer wrong = f.transport.request("/api/restart", HttpMethod::Post, context);
    TEST_ASSERT_EQUAL_UINT(401U, wrong.status);
    TEST_ASSERT_EQUAL_UINT(401U,
        f.transport.request("/update", HttpMethod::Get, context).status);
    TEST_ASSERT_EQUAL_UINT(0U, f.bridge.pendingCount());
    TEST_ASSERT_FALSE(f.app.restartPending());
}

void test_update_page_and_bodyless_restart() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    Context context;
    context.authorization = "Basic dGVzdC11c2VyOnRlc3QtcGFzc3dvcmQ=";
    Writer page = f.transport.request("/update", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, page.status);
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(ContentType::Html), static_cast<unsigned>(page.contentType));
    TEST_ASSERT_TRUE(page.body.find("firmware.bin") != std::string::npos);
    TEST_ASSERT_TRUE(page.body.find("x.open('POST','/update')") != std::string::npos);
    TEST_ASSERT_TRUE(page.body.find("X-Firmware-Size") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(413U, f.transport.request("/api/restart", HttpMethod::Post, context, 1U).status);
    TEST_ASSERT_EQUAL_UINT(0U, f.bridge.pendingCount());
}

void test_restart_completion_duplicate_and_deadline() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    Context context;
    context.authorization = "Basic dGVzdC11c2VyOnRlc3QtcGFzc3dvcmQ=";
    f.lock.onWait = processOne;
    f.lock.waitContext = &f.app;
    f.authority.now = 100U;
    Writer first = f.transport.request("/api/restart", HttpMethod::Post, context);
    TEST_ASSERT_EQUAL_UINT(202U, first.status);
    TEST_ASSERT_TRUE(first.body.find("Restart zaplanowany") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(1000U, f.lock.lastTimeout);
    TEST_ASSERT_TRUE(f.app.restartPending());
    TEST_ASSERT_EQUAL_UINT(1100U, f.app.restartAt());
    TEST_ASSERT_EQUAL_STRING("", f.authority.order.c_str());
    f.authority.now = 500U;
    TEST_ASSERT_EQUAL_UINT(202U, f.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_EQUAL_UINT(1500U, f.app.restartAt());
    f.authority.now = 1499U;
    f.app.serviceRestart();
    TEST_ASSERT_EQUAL_STRING("", f.authority.order.c_str());
    f.authority.now = 1500U;
    f.app.serviceRestart();
    TEST_ASSERT_EQUAL_STRING("PR", f.authority.order.c_str());
    TEST_ASSERT_FALSE(f.app.restartPending());
    f.app.serviceRestart();
    TEST_ASSERT_EQUAL_STRING("PR", f.authority.order.c_str());
}

void test_queue_full_and_accepted_timeout() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    ApplicationBridgeToken tokens[4U] {};
    const DoserWebRequest command {DoserWebRequestKind::ScheduleRestart};
    for (size_t i = 0U; i < 4U; ++i)
        TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(ApplicationBridgeSubmitResult::Accepted),
            static_cast<unsigned>(f.bridge.submit(command, tokens[i])));
    Context context;
    context.authorization = "Basic dGVzdC11c2VyOnRlc3QtcGFzc3dvcmQ=";
    TEST_ASSERT_EQUAL_UINT(503U, f.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_EQUAL_UINT(4U, f.bridge.pendingCount());
    Fixture delayed;
    TEST_ASSERT_TRUE(delayed.begin());
    Writer uncertain = delayed.transport.request("/api/restart", HttpMethod::Post, context);
    TEST_ASSERT_EQUAL_UINT(202U, uncertain.status);
    TEST_ASSERT_TRUE(uncertain.body.find("outcome unknown") != std::string::npos);
    TEST_ASSERT_FALSE(delayed.app.restartPending());
    TEST_ASSERT_EQUAL_UINT(1U, delayed.bridge.pendingCount());
    TEST_ASSERT_TRUE(delayed.app.processOne());
    TEST_ASSERT_TRUE(delayed.app.restartPending());
    TEST_ASSERT_EQUAL_UINT(0U, delayed.bridge.pendingCount());
    Fixture failedWait;
    TEST_ASSERT_TRUE(failedWait.begin());
    failedWait.lock.failWait = true;
    Writer unknown = failedWait.transport.request("/api/restart", HttpMethod::Post, context);
    TEST_ASSERT_EQUAL_UINT(202U, unknown.status);
    TEST_ASSERT_TRUE(unknown.body.find("outcome unknown") != std::string::npos);
    TEST_ASSERT_TRUE(failedWait.app.processOne());
    TEST_ASSERT_TRUE(failedWait.app.restartPending());
    Fixture failedSubmit;
    TEST_ASSERT_TRUE(failedSubmit.begin());
    failedSubmit.lock.failNextLock = true;
    TEST_ASSERT_EQUAL_UINT(503U,
        failedSubmit.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_EQUAL_UINT(0U, failedSubmit.bridge.pendingCount());
}

void test_web_failure_keeps_application_available() {
    Fixture f;
    f.transport.failBegin = true;
    TEST_ASSERT_FALSE(f.begin());
    ApplicationBridgeToken token {};
    const DoserWebRequest command {DoserWebRequestKind::ScheduleRestart};
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(ApplicationBridgeSubmitResult::Accepted),
        static_cast<unsigned>(f.bridge.submit(command, token)));
    TEST_ASSERT_TRUE(f.app.processOne());
    TEST_ASSERT_TRUE(f.app.restartPending());
}

void test_ota_busy_get_and_manual_restart_admission() {
    Fixture f;
    TEST_ASSERT_TRUE(f.begin());
    Context context;
    context.authorization = "Basic dGVzdC11c2VyOnRlc3QtcGFzc3dvcmQ=";
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.upload.beginSession(generation));
    TEST_ASSERT_EQUAL_UINT(409U,
        f.transport.request("/update", HttpMethod::Get, context).status);
    context.authorization = nullptr;
    TEST_ASSERT_EQUAL_UINT(401U,
        f.transport.request("/update", HttpMethod::Get, context).status);
    context.authorization = "Basic dGVzdC11c2VyOnRlc3QtcGFzc3dvcmQ=";
    f.lock.onWait = processOne;
    f.lock.waitContext = &f.app;
    TEST_ASSERT_EQUAL_UINT(409U,
        f.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_FALSE(f.app.restartPending());
    TEST_ASSERT_TRUE(f.upload.releaseSession(generation));
    TEST_ASSERT_EQUAL_UINT(200U,
        f.transport.request("/update", HttpMethod::Get, context).status);
}
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_inventory_and_read_routes);
    RUN_TEST(test_unpublished_and_snapshot_isolation);
    RUN_TEST(test_diagnostics_source_preserves_unknown_storage_and_real_time);
    RUN_TEST(test_disabled_and_failed_auth_do_not_submit);
    RUN_TEST(test_update_page_and_bodyless_restart);
    RUN_TEST(test_restart_completion_duplicate_and_deadline);
    RUN_TEST(test_queue_full_and_accepted_timeout);
    RUN_TEST(test_web_failure_keeps_application_available);
    RUN_TEST(test_ota_busy_get_and_manual_restart_admission);
    return UNITY_END();
}
