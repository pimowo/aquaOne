#include <unity.h>

#include <cstring>
#include <string>

#include <AquaCore/System/SystemService.h>
#include <AquaCore/Web/CoreWebProjectionSources.h>
#include <AquaCore/Web/HttpRouteRegistry.h>

#include "../../src/DoserNativeWebStartup.h"
#include "../../src/DoserDiagnosticsProjection.h"

using namespace AquaCore;
using namespace AquaCore::Web;

namespace {
class Sync final : public SnapshotSynchronizer, public ActionBridgeSynchronizer {
public:
    bool lock() override { return !failLock; }
    void unlock() override {}
    size_t completionSlotCapacity() const override { return 4U; }
    bool prepareCompletion(size_t slot) override { signaled[slot] = false; return true; }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t) override {
        if (onWait != nullptr) onWait(waitContext);
        return signaled[slot] ? ActionBridgeWaitStatus::Signaled
                              : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override { signaled[slot] = true; return true; }
    bool failLock = false;
    bool signaled[4U] {};
    void (*onWait)(void*) = nullptr;
    void* waitContext = nullptr;
};

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t value, ContentType) override { status = value; return true; }
    bool write(const char* bytes, size_t length) override {
        body.append(bytes, length); return true;
    }
    bool endResponse() override { return true; }
    uint16_t status = 0U;
    std::string body;
};

class Context final : public WebRequestContext {
public:
    bool hasHeader(const char*) const override { return false; }
    size_t copyHeader(const char*, char*, size_t) const override { return 0U; }
    bool authenticateBasic(const char* user, const char* password) const override {
        return authorized && std::strcmp(user, "user") == 0 &&
               std::strcmp(password, "password") == 0;
    }
    bool requestBasicAuthentication(const char*) const override {
        if (writer == nullptr) return false;
        writer->beginResponse(401U, ContentType::PlainText);
        return writer->endResponse();
    }
    bool authorized = false;
    mutable Writer* writer = nullptr;
};

class Transport final : public HttpServerTransport, public HttpStreamingServerTransport {
public:
    bool addRoute(const char* path, HttpMethod method, HttpRouteHandler handler,
                  void* context, const HttpRouteOptions& options) override {
        return !(failNormal && std::strcmp(path, "/api/restart") == 0) &&
               routes.addRoute(path, method, handler, context, options);
    }
    bool addStreamingRoute(const char* path, HttpMethod method,
                           HttpStreamHandler handler, void* context,
                           size_t maximum) override {
        return !failStreaming &&
               routes.addStreamingRoute(path, method, handler, context, maximum);
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) override {
        return routes.setNotFoundHandler(handler, context);
    }
    bool begin(uint16_t port) override {
        ++beginCalls;
        beginPort = port;
        CoreSystemProjection systemValue {};
        CoreDiagnosticsProjection diagnosticsValue {};
        DoserOtaCapacity capacityValue {};
        snapshotsReadyAtBegin = system->read(systemValue) &&
            diagnostics->read(diagnosticsValue) && capacity->read(capacityValue) &&
            capacityValue.available && capacityValue.availableBytes == 1310720U;
        running = !failBegin;
        return running;
    }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }
    Writer request(const char* path, HttpMethod method, Context& context) {
        Writer writer;
        context.writer = &writer;
        for (size_t i = 0U; i < routes.size(); ++i) {
            const HttpRouteRegistry::Route* route = routes.routeAt(i);
            if (route->method != method || std::strcmp(route->path, path) != 0) continue;
            if (route->kind != HttpRouteRegistry::RouteKind::Normal) return writer;
            const HttpRouteRequest request {method, path, nullptr, 0U, &context};
            route->handler(route->context, request, writer);
            return writer;
        }
        routes.notFoundHandler()(routes.notFoundContext(), writer);
        return writer;
    }
    HttpRouteRegistry routes;
    const PublishedSnapshot<CoreSystemProjection>* system = nullptr;
    const PublishedSnapshot<CoreDiagnosticsProjection>* diagnostics = nullptr;
    const DoserOtaCapacitySnapshot* capacity = nullptr;
    bool failNormal = false;
    bool failStreaming = false;
    bool failBegin = false;
    bool running = false;
    bool snapshotsReadyAtBegin = false;
    unsigned beginCalls = 0U;
    uint16_t beginPort = 0U;
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
        out = value; return true;
    }
    DoserDiagnosticsFacts value {};
};

class Restart final : public DoserRestartAuthority {
public:
    uint32_t nowMs() const override { return now; }
    void stopPumps() override { ++stops; }
    void restartDevice() override { ++restarts; }
    uint32_t now = 0U;
    unsigned stops = 0U;
    unsigned restarts = 0U;
};

class Ota final : public DoserOtaAuthority, public DoserOtaClock {
public:
    uint32_t nowMs() const override { return 0U; }
    size_t availableFirmwareSpace() const override { return available; }
    bool beginFirmwareUpdate(size_t) override { return true; }
    size_t writeFirmware(const uint8_t*, size_t length) override { return length; }
    bool endFirmwareUpdate() override { return true; }
    void abortFirmwareUpdate() override {}
    const char* firmwareError() const override { return "error"; }
    void stopPumps() override {}
    void setOtaInProgress(bool) override {}
    size_t available = 1310720U;
};

class Fixture {
public:
    Fixture()
        : system(backend), systemSource(system, canonicalIdentity), diagnosticsSource(facts),
          systemSnapshot(systemSync), diagnosticsSnapshot(diagnosticsSync),
          publisher(systemSource, &diagnosticsSource,
                    systemSnapshot, diagnosticsSnapshot),
          webBridge(normalSync), webApplication(webBridge, restart),
          upload(otaSync), capacitySnapshot(capacitySync),
          otaApplication(upload, ota, webApplication, capacitySnapshot),
          normalRoutes(webBridge, "user", "password", &upload),
          otaRoute(upload, capacitySnapshot, ota, "user", "password"),
          service(transport, systemSnapshot, diagnosticsSnapshot) {
        TEST_ASSERT_TRUE(system.begin(DeviceIdentity("Doser", "Test", "1", "S3")));
        const uint8_t bytes[] {0x24U, 0x6FU, 0x28U, 0xA1U, 0xB2U, 0xC3U};
        Identity::DeviceId id;
        TEST_ASSERT_TRUE(id.assign(bytes, sizeof(bytes)).isValid());
        TEST_ASSERT_TRUE(canonicalIdentity.assign("doser", id).isValid());
        facts.value.systemReady = true;
        facts.value.productHealth = Diagnostics::HealthState::Ok;
        transport.system = &systemSnapshot;
        transport.diagnostics = &diagnosticsSnapshot;
        transport.capacity = &capacitySnapshot;
        webApplication.setOtaSessionView(&upload);
        normalSync.onWait = processWeb;
        normalSync.waitContext = this;
    }
    static void processWeb(void* context) {
        static_cast<Fixture*>(context)->webApplication.processOne();
    }
    bool start(size_t capacity = 1310720U) {
        return startDoserNativeWeb(service, transport, publisher, otaApplication,
                                   capacitySnapshot, normalRoutes, otaRoute, capacity);
    }
    Backend backend;
    SystemService system;
    Identity::DeviceIdentity canonicalIdentity;
    SystemServiceWebProjectionSource systemSource;
    Facts facts;
    DoserDiagnosticsProjectionSource diagnosticsSource;
    Sync systemSync;
    PublishedSnapshot<CoreSystemProjection> systemSnapshot;
    Sync diagnosticsSync;
    PublishedSnapshot<CoreDiagnosticsProjection> diagnosticsSnapshot;
    CoreWebProjectionPublisher publisher;
    Transport transport;
    Sync normalSync;
    DoserWebBridge webBridge;
    Restart restart;
    DoserWebApplication webApplication;
    Sync otaSync;
    StreamingUploadBridge upload;
    Sync capacitySync;
    DoserOtaCapacitySnapshot capacitySnapshot;
    Ota ota;
    DoserOtaApplication otaApplication;
    DoserNativeWebRoutes normalRoutes;
    DoserNativeOtaRoute otaRoute;
    NativeWebService service; // Stops callbacks before every borrowed context dies.
};

void test_exact_single_transport_inventory_and_initial_snapshots() {
    Fixture f;
    TEST_ASSERT_TRUE(f.start());
    TEST_ASSERT_TRUE(f.transport.snapshotsReadyAtBegin);
    TEST_ASSERT_EQUAL_UINT(1U, f.transport.beginCalls);
    TEST_ASSERT_EQUAL_UINT(80U, f.transport.beginPort);
    struct ExpectedRoute {
        const char* path;
        HttpMethod method;
        HttpRouteRegistry::RouteKind kind;
    };
    const ExpectedRoute expected[] = {
        {"/", HttpMethod::Get, HttpRouteRegistry::RouteKind::Normal},
        {"/assets/aqua.css", HttpMethod::Get, HttpRouteRegistry::RouteKind::Normal},
        {"/api/system", HttpMethod::Get, HttpRouteRegistry::RouteKind::Normal},
        {"/api/diagnostics", HttpMethod::Get, HttpRouteRegistry::RouteKind::Normal},
        {"/api/restart", HttpMethod::Post, HttpRouteRegistry::RouteKind::Normal},
        {"/update", HttpMethod::Get, HttpRouteRegistry::RouteKind::Normal},
        {"/update", HttpMethod::Post, HttpRouteRegistry::RouteKind::Streaming}
    };
    bool seen[7U] {};
    TEST_ASSERT_EQUAL_UINT(7U, f.transport.routes.size());
    for (size_t i = 0U; i < 7U; ++i) {
        const HttpRouteRegistry::Route* route = f.transport.routes.routeAt(i);
        bool matched = false;
        for (size_t j = 0U; j < 7U; ++j) {
            if (std::strcmp(route->path, expected[j].path) != 0 ||
                route->method != expected[j].method ||
                route->kind != expected[j].kind) continue;
            TEST_ASSERT_FALSE(seen[j]);
            seen[j] = true;
            matched = true;
            if (route->kind == HttpRouteRegistry::RouteKind::Streaming)
                TEST_ASSERT_EQUAL_UINT(1310720U + 664U,
                                       route->options.maxBodyLength);
            break;
        }
        TEST_ASSERT_TRUE(matched);
    }
    for (size_t i = 0U; i < 7U; ++i) TEST_ASSERT_TRUE(seen[i]);
    Context context;
    TEST_ASSERT_EQUAL_UINT(404U,
        f.transport.request("/api/status", HttpMethod::Get, context).status);
    Writer initialSystem = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, initialSystem.status);
    TEST_ASSERT_TRUE(initialSystem.body.find("\"uptimeMs\":100") != std::string::npos);
    Writer diagnostics = f.transport.request("/api/diagnostics", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, diagnostics.status);
    TEST_ASSERT_TRUE(diagnostics.body.find("\"storage\":{\"health\":\"unknown\"")
                     != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(401U,
        f.transport.request("/update", HttpMethod::Get, context).status);
    context.authorized = true;
    Writer page = f.transport.request("/update", HttpMethod::Get, context);
    TEST_ASSERT_EQUAL_UINT(200U, page.status);
    TEST_ASSERT_TRUE(page.body.find("firmware.bin") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(202U,
        f.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_TRUE(f.webApplication.restartPending());
    f.backend.uptime = 250U;
    TEST_ASSERT_TRUE(f.publisher.update().systemPublished);
    Writer system = f.transport.request("/api/system", HttpMethod::Get, context);
    TEST_ASSERT_TRUE(system.body.find("\"uptimeMs\":250") != std::string::npos);
}

void test_ota_session_blocks_normal_restart() {
    Fixture f;
    TEST_ASSERT_TRUE(f.start());
    uint64_t generation = 0U;
    TEST_ASSERT_TRUE(f.upload.beginSession(generation));
    Context context;
    context.authorized = true;
    TEST_ASSERT_EQUAL_UINT(409U,
        f.transport.request("/api/restart", HttpMethod::Post, context).status);
    TEST_ASSERT_FALSE(f.webApplication.restartPending());
}

void test_registration_failures_never_begin() {
    Fixture normal;
    normal.transport.failNormal = true;
    TEST_ASSERT_FALSE(normal.start());
    TEST_ASSERT_EQUAL_UINT(0U, normal.transport.beginCalls);
    TEST_ASSERT_FALSE(normal.transport.running);
    Fixture streaming;
    streaming.transport.failStreaming = true;
    TEST_ASSERT_FALSE(streaming.start());
    TEST_ASSERT_EQUAL_UINT(0U, streaming.transport.beginCalls);
    TEST_ASSERT_FALSE(streaming.transport.running);
}

void test_publication_and_capacity_failures_never_begin() {
    Fixture publication;
    publication.diagnosticsSync.failLock = true;
    TEST_ASSERT_FALSE(publication.start());
    TEST_ASSERT_EQUAL_UINT(0U, publication.transport.beginCalls);
    Fixture capacity;
    TEST_ASSERT_FALSE(capacity.start(0U));
    TEST_ASSERT_EQUAL_UINT(0U, capacity.transport.beginCalls);
    Fixture mismatch;
    TEST_ASSERT_FALSE(mismatch.start(100U));
    TEST_ASSERT_EQUAL_UINT(0U, mismatch.transport.beginCalls);
}

void test_begin_failure_preserves_application() {
    Fixture f;
    f.transport.failBegin = true;
    TEST_ASSERT_FALSE(f.start());
    TEST_ASSERT_EQUAL_UINT(1U, f.transport.beginCalls);
    TEST_ASSERT_FALSE(f.transport.running);
    ApplicationBridgeToken token {};
    const DoserWebRequest request {DoserWebRequestKind::ScheduleRestart};
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(ApplicationBridgeSubmitResult::Accepted),
        static_cast<unsigned>(f.webBridge.submit(request, token)));
    TEST_ASSERT_TRUE(f.webApplication.processOne());
    TEST_ASSERT_TRUE(f.webApplication.restartPending());
}
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_exact_single_transport_inventory_and_initial_snapshots);
    RUN_TEST(test_ota_session_blocks_normal_restart);
    RUN_TEST(test_registration_failures_never_begin);
    RUN_TEST(test_publication_and_capacity_failures_never_begin);
    RUN_TEST(test_begin_failure_preserves_application);
    return UNITY_END();
}
