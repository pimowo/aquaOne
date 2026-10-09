#include <unity.h>

#include <cstring>
#include <string>

#include "AquaCore/System/ApplicationRuntime.h"
#include "AquaCore/System/RuntimeStateCoordinator.h"
#include "AquaCore/Web/CoreWebProjectionPublisher.h"
#include "AquaCore/Web/CoreWebProjectionSources.h"
#include "AquaCore/Web/NativeWebService.h"
#include "AquaCore/Web/WebHealthProvider.h"
#include "AquaCore/Web/WebStartup.h"

using namespace AquaCore;
using namespace AquaCore::Web;

namespace {

class TestSynchronizer final : public SnapshotSynchronizer {
public:
    bool lock() override {
        if (!lockAllowed) return false;
        locked = true;
        return true;
    }
    void unlock() override { locked = false; }
    bool locked = false;
    bool lockAllowed = true;
};

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t value, ContentType content) override {
        status = value;
        type = content;
        body.clear();
        return true;
    }
    bool write(const char* data, size_t length) override {
        TEST_ASSERT_FALSE(snapshotLock != nullptr && snapshotLock->locked);
        body.append(data, length);
        return true;
    }
    bool endResponse() override { ended = true; return true; }

    uint16_t status = 0U;
    ContentType type = ContentType::PlainText;
    std::string body;
    bool ended = false;
    TestSynchronizer* snapshotLock = nullptr;
};

class FakeTransport final : public HttpServerTransport {
public:
    struct Route {
        const char* path = nullptr;
        HttpMethod method = HttpMethod::Get;
        HttpRouteHandler handler = nullptr;
        void* context = nullptr;
        HttpRouteOptions options {};
    };

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context,
                  const HttpRouteOptions& options) override {
        ++addAttempts;
        if (failAddAttempt != 0U && addAttempts == failAddAttempt) {
            return false;
        }
        if (frozen || count == 8U || path == nullptr || handler == nullptr) {
            return false;
        }
        for (size_t i = 0U; i < count; ++i) {
            if (routes[i].method == method && std::strcmp(routes[i].path, path) == 0) {
                return false;
            }
        }
        Route& route = routes[count++];
        route.path = path;
        route.method = method;
        route.handler = handler;
        route.context = context;
        route.options = options;
        ++addCalls;
        return true;
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) override {
        if (frozen || notFound != nullptr || handler == nullptr) return false;
        notFound = handler;
        notFoundContext = context;
        return true;
    }
    bool begin(uint16_t value) override {
        ++beginCalls;
        port = value;
        running = beginResult;
        if (running) frozen = true;
        return running;
    }
    void stop() override { ++stopCalls; running = false; }
    bool isRunning() const override { return running; }

    Writer request(const char* path, HttpMethod method = HttpMethod::Get,
                   TestSynchronizer* lock = nullptr) {
        Writer writer;
        writer.snapshotLock = lock;
        for (size_t i = 0U; i < count; ++i) {
            if (routes[i].method == method && std::strcmp(routes[i].path, path) == 0) {
                const HttpRouteRequest request {method, path, nullptr, 0U};
                routes[i].handler(routes[i].context, request, writer);
                return writer;
            }
        }
        if (notFound != nullptr) notFound(notFoundContext, writer);
        return writer;
    }

    Route routes[8] {};
    size_t count = 0U;
    size_t addCalls = 0U;
    size_t addAttempts = 0U;
    size_t failAddAttempt = 0U;
    size_t beginCalls = 0U;
    size_t stopCalls = 0U;
    uint16_t port = 0U;
    bool beginResult = true;
    bool running = false;
    bool frozen = false;
    HttpNotFoundHandler notFound = nullptr;
    void* notFoundContext = nullptr;
};

class SystemSource final : public CoreSystemProjectionSource {
public:
    bool read(CoreSystemProjection& out) const override {
        if (!available) return false;
        out = value;
        return true;
    }
    bool available = true;
    CoreSystemProjection value {};
};

class DiagnosticsSource final : public CoreDiagnosticsProjectionSource {
public:
    bool read(CoreDiagnosticsProjection& out) const override {
        if (!available) return false;
        out = value;
        return true;
    }
    bool available = true;
    CoreDiagnosticsProjection value {};
};

class StaticPage final : public WebPageProvider {
public:
    StaticPage(const char* path, const char* pageTitle, const char* body)
        : path_(path), title_(pageTitle), body_(body) {}
    const char* route() const override { return path_; }
    const char* title() const override { return title_; }
    void render(WebResponseWriter& response) const override {
        response.writeText(body_);
    }
private:
    const char* path_;
    const char* title_;
    const char* body_;
};

struct Fixture {
    Fixture()
        : systemSnapshot(systemLock),
          diagnosticsSnapshot(diagnosticsLock),
          publisher(systemSource, &diagnosticsSource,
                    systemSnapshot, diagnosticsSnapshot),
          service(transport, systemSnapshot, diagnosticsSnapshot) {
        systemSource.value.identity = DeviceIdentity(
            "controller\"type", "Aqua <\"One\">\\Lab\n", "1.2.3", "ESP32"
        );
        std::strcpy(systemSource.value.aquaCoreVersion, "0.6.2");
        systemSource.value.uptimeMs = 100U;
        systemSource.value.restartReason = RestartReason::PowerOn;
        systemSource.value.ready = true;

        Diagnostics::DiagnosticsSnapshot& value = diagnosticsSource.value.value;
        value.overallHealth = Diagnostics::HealthState::Warning;
        value.systemHealth = Diagnostics::HealthState::Ok;
        value.system.ready = true;
        value.timeHealth = Diagnostics::HealthState::Ok;
        value.time.state = Diagnostics::TimeState::Valid;
        value.time.rtcReady = true;
        value.time.rtcValid = true;
        std::strcpy(value.time.providerName, "Europe/\"Warsaw\\\n");
        value.storageHealth = Diagnostics::HealthState::Ok;
        value.storage.backendReady = true;
        value.storage.hasValidPayload = true;
        value.storage.activeSlot = Config::StorageSlot::B;
        value.storage.activeGeneration = 42U;
        value.storage.lastLoadResult = Config::StorageOperationResult::Success;
        value.storage.lastSaveResult = Config::StorageOperationResult::Failure;
        value.networkHealth = Diagnostics::HealthState::Warning;
        value.network.available = true;
        value.network.state = Network::NetworkState::Connected;
        value.network.connected = true;
        std::strcpy(value.network.ssid, "safe\"ssid\\\n");
        std::strcpy(value.network.hostname, "aqua\"one\\host\t");
        value.network.ipAddress.octets[0] = 192U;
        value.network.ipAddress.octets[1] = 168U;
        value.network.ipAddress.octets[2] = 1U;
        value.network.ipAddress.octets[3] = 20U;
        value.network.rssi = -61;
        value.network.reconnectCount = 7U;
        value.network.apActive = true;
        std::strcpy(value.network.apSsid, "setup-ap");
        value.network.apIpAddress.octets[0] = 192U;
        value.network.apIpAddress.octets[1] = 168U;
        value.network.apIpAddress.octets[2] = 4U;
        value.network.apIpAddress.octets[3] = 1U;
    }

    WebConfig enabledConfig() const {
        WebConfig value {};
        value.enabled = true;
        value.port = 8080U;
        return value;
    }

    TestSynchronizer systemLock;
    TestSynchronizer diagnosticsLock;
    PublishedSnapshot<CoreSystemProjection> systemSnapshot;
    PublishedSnapshot<CoreDiagnosticsProjection> diagnosticsSnapshot;
    SystemSource systemSource;
    DiagnosticsSource diagnosticsSource;
    CoreWebProjectionPublisher publisher;
    FakeTransport transport;
    NativeWebService service;
};

void assertContains(const std::string& value, const char* expected) {
    TEST_ASSERT_NOT_EQUAL(std::string::npos, value.find(expected));
}

void testRoutesAndNoPost() {
    Fixture f;
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));
    TEST_ASSERT_EQUAL_UINT32(4U, f.transport.count);
    TEST_ASSERT_NOT_NULL(f.transport.notFound);
    const char* expected[] = {"/", "/assets/aqua.css", "/api/system", "/api/diagnostics"};
    for (size_t i = 0U; i < 4U; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], f.transport.routes[i].path);
        TEST_ASSERT_EQUAL(HttpMethod::Get, f.transport.routes[i].method);
    }
}

void testSystemUnavailableAndSnapshotIndependence() {
    Fixture f;
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));
    Writer unavailable = f.transport.request("/api/system");
    TEST_ASSERT_EQUAL_UINT16(503U, unavailable.status);
    TEST_ASSERT_EQUAL(ContentType::Json, unavailable.type);

    TEST_ASSERT_TRUE(f.publisher.update().systemPublished);
    Writer first = f.transport.request("/api/system", HttpMethod::Get, &f.systemLock);
    TEST_ASSERT_EQUAL_UINT16(200U, first.status);
    TEST_ASSERT_EQUAL(ContentType::Json, first.type);
    assertContains(first.body, "\"deviceType\":\"controller\\\"type\"");
    assertContains(first.body, "\"deviceName\":\"Aqua <\\\"One\\\">\\\\Lab\\n\"");
    assertContains(first.body, "\"firmwareVersion\":\"1.2.3\"");
    assertContains(first.body, "\"hardwareVariant\":\"ESP32\"");
    assertContains(first.body, "\"aquaCoreVersion\":\"0.6.2\"");
    assertContains(first.body, "\"uptimeMs\":100");
    assertContains(first.body, "\"restartReason\":\"POWER_ON\"");

    f.systemSource.value.uptimeMs = 250U;
    Writer stale = f.transport.request("/api/system");
    assertContains(stale.body, "\"uptimeMs\":100");
    f.publisher.update();
    Writer refreshed = f.transport.request("/api/system");
    assertContains(refreshed.body, "\"uptimeMs\":250");

    f.systemSource.available = false;
    TEST_ASSERT_FALSE(f.publisher.update().systemPublished);
    Writer invalidated = f.transport.request("/api/system");
    TEST_ASSERT_EQUAL_UINT16(503U, invalidated.status);
    Writer diagnostics = f.transport.request("/api/diagnostics");
    TEST_ASSERT_EQUAL_UINT16(200U, diagnostics.status);
}

void testDiagnosticsCompatibilityAndIndependence() {
    Fixture f;
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));
    f.diagnosticsSource.available = false;
    f.publisher.update();
    Writer unavailable = f.transport.request("/api/diagnostics");
    TEST_ASSERT_EQUAL_UINT16(503U, unavailable.status);
    Writer system = f.transport.request("/api/system");
    TEST_ASSERT_EQUAL_UINT16(200U, system.status);

    f.diagnosticsSource.available = true;
    f.publisher.update();
    Writer response = f.transport.request(
        "/api/diagnostics", HttpMethod::Get, &f.diagnosticsLock
    );
    TEST_ASSERT_EQUAL_UINT16(200U, response.status);
    TEST_ASSERT_EQUAL(ContentType::Json, response.type);
    assertContains(response.body, "\"health\":\"warning\"");
    assertContains(response.body, "\"system\":{\"health\":\"ok\",\"ready\":true}");
    assertContains(response.body, "\"state\":\"valid\"");
    assertContains(response.body, "\"provider\":\"Europe/\\\"Warsaw\\\\\\n\"");
    assertContains(response.body, "\"generation\":42");
    assertContains(response.body, "\"lastLoad\":\"success\"");
    assertContains(response.body, "\"lastSave\":\"failure\"");
    assertContains(response.body, "\"state\":\"connected\"");
    assertContains(response.body, "\"ssid\":\"safe\\\"ssid\\\\\\n\"");
    assertContains(response.body, "\"hostname\":\"aqua\\\"one\\\\host\\t\"");
    assertContains(response.body, "\"ipAddress\":\"192.168.1.20\"");
    assertContains(response.body, "\"rssi\":-61");
    assertContains(response.body, "\"reconnectCount\":7");
    assertContains(response.body, "\"apActive\":true");
    assertContains(response.body, "\"apSsid\":\"setup-ap\"");
    assertContains(response.body, "\"apIpAddress\":\"192.168.4.1\"");
    TEST_ASSERT_EQUAL(std::string::npos, response.body.find("password"));
    TEST_ASSERT_EQUAL(std::string::npos, response.body.find("token"));
    TEST_ASSERT_EQUAL(std::string::npos, response.body.find("privateKey"));

    f.diagnosticsSource.available = false;
    TEST_ASSERT_FALSE(f.publisher.update().diagnosticsPublished);
    Writer invalidated = f.transport.request("/api/diagnostics");
    TEST_ASSERT_EQUAL_UINT16(503U, invalidated.status);
    Writer stillSystem = f.transport.request("/api/system");
    TEST_ASSERT_EQUAL_UINT16(200U, stillSystem.status);
}

void testDiagnosticsDetailedStorageResults() {
    Fixture f;
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));

    struct ExpectedResult {
        Config::StorageOperationResult result;
        const char* text;
    };
    const ExpectedResult expected[] = {
        {Config::StorageOperationResult::NoChange, "no_change"},
        {Config::StorageOperationResult::InvalidArgument, "invalid_argument"},
        {Config::StorageOperationResult::ValidationFailure, "validation_failure"},
        {Config::StorageOperationResult::BackendFailure, "backend_failure"},
        {Config::StorageOperationResult::VerifyFailure, "verify_failure"}
    };

    for (const ExpectedResult& item : expected) {
        f.diagnosticsSource.value.value.storage.lastSaveResult = item.result;
        TEST_ASSERT_TRUE(f.publisher.update().diagnosticsPublished);
        const Writer response = f.transport.request("/api/diagnostics");
        TEST_ASSERT_EQUAL_UINT16(200U, response.status);
        const std::string field = std::string("\"lastSave\":\"") + item.text + "\"";
        assertContains(response.body, field.c_str());
    }
}

void testRootCssAndNotFound() {
    Fixture f;
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));
    Writer rootUnavailable = f.transport.request("/");
    TEST_ASSERT_EQUAL_UINT16(503U, rootUnavailable.status);
    f.publisher.update();
    Writer root = f.transport.request("/", HttpMethod::Get, &f.systemLock);
    TEST_ASSERT_EQUAL_UINT16(200U, root.status);
    TEST_ASSERT_EQUAL(ContentType::Html, root.type);
    assertContains(root.body, "Aqua &lt;&quot;One&quot;&gt;\\Lab");
    Writer css = f.transport.request("/assets/aqua.css");
    TEST_ASSERT_EQUAL_UINT16(200U, css.status);
    TEST_ASSERT_EQUAL(ContentType::Css, css.type);
    TEST_ASSERT_FALSE(css.body.empty());
    Writer missing = f.transport.request("/missing");
    TEST_ASSERT_EQUAL_UINT16(404U, missing.status);
    TEST_ASSERT_EQUAL(ContentType::PlainText, missing.type);
    TEST_ASSERT_EQUAL_STRING("Not Found", missing.body.c_str());
}

void testBorrowedStaticPagesUseBuiltInRootAndSharedShell() {
    Fixture f;
    StaticPage root("/", "Luma dashboard", "<section>root-page</section>");
    StaticPage control("/control", "Control", "<section>control-page</section>");
    StaticPage reserved("/api/system", "Wrong", "wrong");
    TEST_ASSERT_TRUE(f.service.addPage(root));
    TEST_ASSERT_TRUE(f.service.addPage(control));
    TEST_ASSERT_FALSE(f.service.addPage(reserved));
    TEST_ASSERT_TRUE(f.publisher.update().systemPublished);
    TEST_ASSERT_TRUE(f.service.begin(f.enabledConfig()));
    TEST_ASSERT_EQUAL_UINT32(5U, f.transport.count);
    Writer rootResponse = f.transport.request("/");
    TEST_ASSERT_EQUAL_UINT16(200U, rootResponse.status);
    assertContains(rootResponse.body, "root-page");
    assertContains(rootResponse.body, "Luma dashboard");
    Writer controlResponse = f.transport.request("/control");
    TEST_ASSERT_EQUAL_UINT16(200U, controlResponse.status);
    assertContains(controlResponse.body, "control-page");
    assertContains(controlResponse.body, "href=\"/assets/aqua.css\"");
    StaticPage late("/late", "Late", "late");
    TEST_ASSERT_FALSE(f.service.addPage(late));
}

void testLifecycleDisabledFailureRetryAndRestart() {
    Fixture disabled;
    WebConfig disabledConfig {};
    disabledConfig.port = 0U;
    WebStartup disabledStartup(disabled.service, disabledConfig);
    const System::StartupStepResult disabledResult =
        WebStartup::run(&disabledStartup);
    TEST_ASSERT_EQUAL(System::StartupOutcome::DISABLED, disabledResult.outcome());
    TEST_ASSERT_EQUAL_UINT32(0U, disabled.transport.beginCalls);
    WebHealthProvider disabledHealth(disabled.service);
    TEST_ASSERT_EQUAL(System::HealthState::OK, disabledHealth.healthContribution());

    Fixture f;
    const WebConfig config = f.enabledConfig();
    f.transport.beginResult = false;
    WebStartup startup(f.service, config);
    const System::StartupParticipant participant = startup.participant();
    TEST_ASSERT_EQUAL(System::StartupPhase::INTERFACES_INIT, participant.phase);
    TEST_ASSERT_EQUAL(System::StartupRequirement::OPTIONAL, participant.requirement);
    TEST_ASSERT_EQUAL(System::StartupOutcome::FAILED, WebStartup::run(&startup).outcome());
    TEST_ASSERT_EQUAL(NativeWebState::Failed, f.service.state());
    WebHealthProvider health(f.service);
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, health.healthContribution());

    f.transport.beginResult = true;
    TEST_ASSERT_TRUE(f.service.begin(config));
    TEST_ASSERT_TRUE(f.service.isRunning());
    TEST_ASSERT_EQUAL_UINT32(4U, f.transport.addCalls);
    f.service.stop();
    TEST_ASSERT_EQUAL(NativeWebState::Stopped, f.service.state());
    TEST_ASSERT_TRUE(f.service.begin(config));
    TEST_ASSERT_EQUAL_UINT32(4U, f.transport.addCalls);
    TEST_ASSERT_EQUAL_UINT32(3U, f.transport.beginCalls);
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());

    WebConfig invalid = config;
    invalid.port = 0U;
    TEST_ASSERT_FALSE(f.service.begin(invalid));
    TEST_ASSERT_FALSE(f.transport.running);
    TEST_ASSERT_EQUAL(NativeWebState::Failed, f.service.state());
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, health.healthContribution());
}

void testPartialRegistrationIsTerminalAndRuntimeLossIsRecoverable() {
    Fixture partial;
    const WebConfig partialConfig = partial.enabledConfig();
    partial.transport.failAddAttempt = 3U;
    TEST_ASSERT_FALSE(partial.service.begin(partialConfig));
    TEST_ASSERT_EQUAL(NativeWebState::Failed, partial.service.state());
    TEST_ASSERT_EQUAL_UINT32(3U, partial.transport.addAttempts);
    TEST_ASSERT_EQUAL_UINT32(0U, partial.transport.beginCalls);
    partial.transport.failAddAttempt = 0U;
    TEST_ASSERT_FALSE(partial.service.begin(partialConfig));
    TEST_ASSERT_EQUAL_UINT32(3U, partial.transport.addAttempts);
    TEST_ASSERT_EQUAL_UINT32(0U, partial.transport.beginCalls);

    Fixture recovered;
    const WebConfig recoveredConfig = recovered.enabledConfig();
    TEST_ASSERT_TRUE(recovered.service.begin(recoveredConfig));
    WebHealthProvider health(recovered.service);
    recovered.transport.running = false;
    TEST_ASSERT_FALSE(recovered.service.isRunning());
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, health.healthContribution());
    TEST_ASSERT_TRUE(recovered.service.begin(recoveredConfig));
    TEST_ASSERT_EQUAL_UINT32(4U, recovered.transport.addCalls);
    TEST_ASSERT_TRUE(recovered.service.isRunning());
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
}

class ProjectionSystemBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override { return 91U; }
    RestartReason restartReason() const override { return RestartReason::Software; }
};

void testSystemServiceSourceRequiresReadyAuthority() {
    ProjectionSystemBackend backend;
    SystemService system(backend);
    SystemServiceWebProjectionSource source(system);
    CoreSystemProjection value {};
    TEST_ASSERT_FALSE(source.read(value));
    const DeviceIdentity identity("controller", "Aqua", "1.0", "ESP32");
    TEST_ASSERT_TRUE(system.begin(identity));
    TEST_ASSERT_TRUE(source.read(value));
    TEST_ASSERT_TRUE(value.ready);
    TEST_ASSERT_EQUAL_UINT32(91U, value.uptimeMs);
}

System::StartupStepResult success(void*) {
    return System::StartupStepResult::succeeded();
}

void testSys106HandoffKeepsHistoryAndRecoversLiveHealth() {
    Fixture f;
    const WebConfig config = f.enabledConfig();
    f.transport.beginResult = false;
    WebStartup webStartup(f.service, config);
    const System::StartupParticipant participants[] = {webStartup.participant()};
    const System::ApplicationPlan plan {
        {"safe", success, nullptr},
        {"safety", success, nullptr},
        participants,
        1U
    };
    System::StartupFailureRecord failureStorage[1];
    System::ApplicationRuntime runtime(
        plan, {failureStorage, 1U}
    );
    const System::StartupReport& report = runtime.start();
    TEST_ASSERT_TRUE(report.isComplete());
    TEST_ASSERT_FALSE(report.hasFatalFailure());
    TEST_ASSERT_EQUAL_UINT32(1U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL(System::OperationalState::RUNNING, runtime.status().operational);
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, runtime.status().health);

    WebHealthProvider health(f.service);
    const System::HealthProvider* providers[] = {&health};
    System::RuntimeStateCoordinator coordinator(providers, 1U, nullptr, 0U);
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    TEST_ASSERT_TRUE(coordinator.isActive());
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, runtime.status().health);

    f.transport.beginResult = true;
    TEST_ASSERT_TRUE(f.service.begin(config));
    TEST_ASSERT_EQUAL(
        System::RuntimeStateRefreshResult::REFRESHED,
        coordinator.refresh()
    );
    TEST_ASSERT_EQUAL(System::HealthState::OK, runtime.status().health);
    TEST_ASSERT_EQUAL_UINT32(1U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, report.finalStatus().health);
}

void runTests() {
    RUN_TEST(testRoutesAndNoPost);
    RUN_TEST(testSystemUnavailableAndSnapshotIndependence);
    RUN_TEST(testDiagnosticsCompatibilityAndIndependence);
    RUN_TEST(testDiagnosticsDetailedStorageResults);
    RUN_TEST(testRootCssAndNotFound);
    RUN_TEST(testBorrowedStaticPagesUseBuiltInRootAndSharedShell);
    RUN_TEST(testLifecycleDisabledFailureRetryAndRestart);
    RUN_TEST(testPartialRegistrationIsTerminalAndRuntimeLossIsRecoverable);
    RUN_TEST(testSystemServiceSourceRequiresReadyAuthority);
    RUN_TEST(testSys106HandoffKeepsHistoryAndRecoversLiveHealth);
}

} // namespace

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
void setup() { delay(2000); UNITY_BEGIN(); runTests(); UNITY_END(); }
void loop() {}
#else
int main(int, char**) { UNITY_BEGIN(); runTests(); return UNITY_END(); }
#endif
