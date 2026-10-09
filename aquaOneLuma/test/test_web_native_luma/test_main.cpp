#include <Arduino.h>
#include <unity.h>

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>

#define LUMASENSE_STORAGE_PREFERENCES_HEADER "../../test/test_ac8/Preferences.h"
#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_ac8/Wire.h"

#include "../test_ac8/Preferences.h"
#include "../test_ac8/Wire.h"

TwoWire Wire;

#include "../../src/storage/ConfigDefaults.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/storage/StorageService.cpp"
#include "../../src/time/RtcService.cpp"
#include "../../src/time/TimeService.cpp"
#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/core/LumaCore.cpp"
#include "../../src/app/FirmwareApp.cpp"
#include "../../src/web/LumaPages.cpp"
#include "../../src/web/LumaApi.cpp"
#include "../../src/web/LumaNativeWeb.cpp"

#include "AquaCore/Web/Esp32ActionBridgeSynchronizer.h"
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"

using namespace AquaCore;
using namespace AquaCore::Web;
using namespace LumaSense;
using namespace LumaSense::Web;

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

class Writer final : public WebResponseWriter {
public:
    static constexpr size_t kBodyCapacity = 24576U;

    Writer() : body(new (std::nothrow) char[kBodyCapacity]()) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) noexcept = default;
    Writer& operator=(Writer&&) noexcept = default;

    bool beginResponse(uint16_t value, ContentType content) override {
        if (!body) return false;
        status = value; type = content; size = 0U; body[0] = '\0'; return true;
    }
    bool write(const char* data, size_t length) override {
        if (!body || data == nullptr || size >= kBodyCapacity ||
            length >= kBodyCapacity - size) return false;
        std::memcpy(body.get() + size, data, length);
        size += length; body[size] = '\0'; return true;
    }
    bool endResponse() override { ended = true; return true; }
    const char* text() const { return body ? body.get() : ""; }
    uint16_t status = 0U;
    ContentType type = ContentType::PlainText;
    std::unique_ptr<char[]> body;
    size_t size = 0U;
    bool ended = false;
};

static_assert(sizeof(Writer) <= 32U, "Writer must stay small enough for loopTask");
static_assert(std::is_nothrow_move_constructible<Writer>::value,
              "Writer must move safely when returned by value");
static_assert(!std::is_copy_constructible<Writer>::value,
              "Writer must not copy its response buffer");

class Transport final : public HttpServerTransport {
public:
    struct Route {
        char path[64] {};
        HttpMethod method = HttpMethod::Get;
        HttpRouteHandler handler = nullptr;
        void* context = nullptr;
        HttpRouteOptions options {};
    };
    bool addRoute(const char* path, HttpMethod method, HttpRouteHandler handler,
                  void* context, const HttpRouteOptions& options) override {
        if (rejectNextRoute) {
            rejectNextRoute = false;
            return false;
        }
        if (path == nullptr || handler == nullptr || count == 16U) return false;
        for (size_t i = 0U; i < count; ++i) {
            if (routes[i].method == method && std::strcmp(routes[i].path, path) == 0)
                return false;
        }
        std::strncpy(routes[count].path, path, sizeof(routes[count].path) - 1U);
        routes[count].method = method;
        routes[count].handler = handler;
        routes[count].context = context;
        routes[count].options = options;
        ++count;
        return true;
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) override {
        notFound = handler; notFoundContext = context; return handler != nullptr;
    }
    bool begin(uint16_t) override { running = beginResult; return running; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }
    Writer request(const char* path, HttpMethod method = HttpMethod::Get,
                   const char* body = nullptr) {
        Writer output;
        const size_t length = body != nullptr ? std::strlen(body) : 0U;
        for (size_t i = 0U; i < count; ++i) {
            if (routes[i].method == method && std::strcmp(routes[i].path, path) == 0) {
                const HttpRouteRequest request {method, path, body, length};
                routes[i].handler(routes[i].context, request, output);
                return output;
            }
        }
        if (notFound != nullptr) notFound(notFoundContext, output);
        return output;
    }
    const Route* find(const char* path, HttpMethod method) const {
        for (size_t i = 0U; i < count; ++i)
            if (routes[i].method == method && std::strcmp(routes[i].path, path) == 0)
                return &routes[i];
        return nullptr;
    }
    Route routes[16] {};
    size_t count = 0U;
    bool beginResult = true;
    bool rejectNextRoute = false;
    bool running = false;
    HttpNotFoundHandler notFound = nullptr;
    void* notFoundContext = nullptr;
};

class HookSynchronizer final : public ActionBridgeSynchronizer {
public:
    bool lock() override {
        if (locked) return false;
        locked = true;
        return true;
    }
    void unlock() override { locked = false; }
    size_t completionSlotCapacity() const override { return 4U; }
    bool prepareCompletion(size_t slot) override {
        if (slot >= 4U) return false;
        ready[slot] = false;
        return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t) override {
        if (slot >= 4U) return ActionBridgeWaitStatus::Failure;
        if (bridge != nullptr && executor != nullptr)
            (void)bridge->processOne(executor, executorContext);
        return ready[slot] ? ActionBridgeWaitStatus::Signaled
                           : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override {
        if (slot >= 4U) return false;
        ready[slot] = true;
        return true;
    }
    LumaApplicationBridge* bridge = nullptr;
    LumaApplicationBridge::Executor executor = nullptr;
    void* executorContext = nullptr;
    bool ready[4] {};
    bool locked = false;
};

uint8_t bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

void fillRtc() {
    Wire.reg(0x00) = bcd(5U); Wire.reg(0x01) = bcd(30U);
    Wire.reg(0x02) = bcd(10U); Wire.reg(0x03) = bcd(1U);
    Wire.reg(0x04) = bcd(10U); Wire.reg(0x05) = bcd(9U);
    Wire.reg(0x06) = bcd(26U); Wire.reg(0x0F) = 0U;
}

class Hardware final : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override {
        ready = channels != nullptr; return ready;
    }
    bool setChannelPercent(uint8_t channel, float value) override {
        if (!ready || channel >= CHANNEL_COUNT) return false;
        levels[channel] = value; return true;
    }
    bool allChannelsOff() override { return ready; }
    bool isReady() const override { return ready; }
    uint8_t availableChannelCount() const override { return CHANNEL_COUNT; }
    bool isChannelAvailable(uint8_t channel) const override {
        return channel < CHANNEL_COUNT;
    }
    bool ready = false;
    float levels[CHANNEL_COUNT] {};
};

class SystemBackend final : public AquaCore::SystemBackend {
public:
    uint32_t uptimeMs() const override { return 10U; }
    RestartReason restartReason() const override { return RestartReason::PowerOn; }
};

class NetworkBackend final : public AquaCore::Network::NetworkBackend {
public:
    bool applyRadioPolicy(
        AquaCore::Network::TriStateSetting,
        AquaCore::Network::TriStateSetting,
        AquaCore::Network::WifiPowerSaveMode
    ) override { return true; }
    bool setHostname(const char*) override { return true; }
    bool beginSta(const char*, const char*) override { return true; }
    AquaCore::Network::BackendStaState staState() const override {
        return AquaCore::Network::BackendStaState::Connected;
    }
    bool reconnectSta() override { return true; }
    bool disconnectSta() override { return true; }
    AquaCore::Network::IpAddress localIp() const override {
        return {{192U, 168U, 1U, 55U}};
    }
    int32_t rssi() const override { return -58; }
    AquaCore::Network::NetworkDisconnectReason consumeDisconnectReason() override {
        return AquaCore::Network::NetworkDisconnectReason::None;
    }
    bool startAccessPoint(const char*, const char*) override { return true; }
    bool stopAccessPoint() override { return true; }
    AquaCore::Network::IpAddress accessPointIp() const override { return {}; }
};

AquaCore::Network::NetworkConfig networkConfig() {
    AquaCore::Network::NetworkConfig value {};
    value.staEnabled = true;
    std::strcpy(value.ssid, "AquaLab");
    std::strcpy(value.password, "secret");
    std::strcpy(value.hostname, "lumasense");
    value.autoReconnect = true;
    return value;
}

struct AuthorityFixture {
    AuthorityFixture()
        : app(hardware, storage, time), system(systemBackend),
          network(networkBackend),
          diagnostics(system, time.rtcService(), storage.aquaService(),
                      "Europe/Warsaw", nullptr, &network),
          status(statusSync), bridge(actionSync),
          webApplication(app, network, diagnostics, status, bridge) {}
    bool start() {
        return system.begin(DeviceIdentity(
                "lighting-controller", "LumaSense", "0.2.1", "TEST")) &&
            app.begin(0U) && network.begin(networkConfig());
    }
    FirmwareCommandResult execute(const LumaWebRequest& request, uint32_t nowMs) {
        ApplicationBridgeToken token {};
        TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                          bridge.submit(request, token));
        TEST_ASSERT_TRUE(webApplication.processOne(nowMs));
        FirmwareCommandResult result = FirmwareCommandResult::Rejected;
        TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed,
                          bridge.wait(token, 0U, result));
        return result;
    }
    Hardware hardware;
    StorageService storage;
    TimeService time;
    FirmwareApp app;
    SystemBackend systemBackend;
    SystemService system;
    NetworkBackend networkBackend;
    AquaCore::Network::NetworkService network;
    AquaCore::Diagnostics::DiagnosticsService diagnostics;
    Esp32SnapshotSynchronizer statusSync;
    PublishedSnapshot<LumaStatusProjection> status;
    Esp32ActionBridgeSynchronizer actionSync;
    LumaApplicationBridge bridge;
    LumaWebApplication webApplication;
};

WebConfig webConfig() {
    WebConfig value {};
    value.enabled = true;
    value.port = 80U;
    value.navigationMask =
        navigationSectionMask(NavigationSection::Dashboard) |
        navigationSectionMask(NavigationSection::Control) |
        navigationSectionMask(NavigationSection::Diagnostics) |
        navigationSectionMask(NavigationSection::System);
    return value;
}

struct Fixture {
    Fixture()
        : systemSnapshot(systemSync), diagnosticsSnapshot(diagnosticsSync),
          statusSnapshot(statusSync), native(transport, systemSnapshot, diagnosticsSnapshot),
          bridge(actionSync), luma(native, statusSnapshot, bridge) {
        actionSync.bridge = &bridge;
        CoreSystemProjection system {};
        system.identity = DeviceIdentity(
            "lighting-controller", "LumaSense", "0.2.1", "LOLIN32_TEST"
        );
        const uint8_t bytes[] {0x24U, 0x6FU, 0x28U, 0xA1U, 0xB2U, 0xC3U};
        Identity::DeviceId id;
        TEST_ASSERT_TRUE(id.assign(bytes, sizeof(bytes)).isValid());
        TEST_ASSERT_TRUE(system.canonicalIdentity.assign("luma", id).isValid());
        std::strcpy(system.aquaCoreVersion, "0.6.2");
        system.ready = true;
        TEST_ASSERT_TRUE(systemSnapshot.publish(system));
        CoreDiagnosticsProjection diagnostics {};
        diagnostics.value.overallHealth = Diagnostics::HealthState::Ok;
        diagnostics.value.systemHealth = Diagnostics::HealthState::Ok;
        diagnostics.value.system.ready = true;
        TEST_ASSERT_TRUE(diagnosticsSnapshot.publish(diagnostics));
    }
    bool start() {
        return luma.registerRoutes() && native.begin(webConfig());
    }
    Esp32SnapshotSynchronizer systemSync;
    Esp32SnapshotSynchronizer diagnosticsSync;
    Esp32SnapshotSynchronizer statusSync;
    PublishedSnapshot<CoreSystemProjection> systemSnapshot;
    PublishedSnapshot<CoreDiagnosticsProjection> diagnosticsSnapshot;
    PublishedSnapshot<LumaStatusProjection> statusSnapshot;
    Transport transport;
    NativeWebService native;
    HookSynchronizer actionSync;
    LumaApplicationBridge bridge;
    LumaNativeWeb luma;
};

Fixture* fixture = nullptr;

void assertContains(const char* text, const char* part) {
    TEST_ASSERT_NOT_NULL(std::strstr(text, part));
}
void assertMissing(const char* text, const char* part) {
    TEST_ASSERT_NULL(std::strstr(text, part));
}

LumaStatusProjection statusValue(uint8_t profile, const char* name) {
    LumaStatusProjection value {};
    value.mode = OperatingMode::Normal;
    value.activeProfile = profile;
    std::strncpy(value.activeProfileName, name, sizeof(value.activeProfileName) - 1U);
    value.dayState = DayState::Night;
    value.localTime.valid = true;
    value.localTime.year = 2026U; value.localTime.month = 9U;
    value.localTime.day = 10U; value.localTime.hour = 10U;
    value.localTime.minute = 30U; value.localTime.second = 5U;
    for (uint8_t i = 0U; i < CHANNEL_COUNT; ++i) {
        value.requestedLevels[i] = static_cast<float>(i + 1U);
        value.finalLevels[i] = static_cast<float>((i + 1U) * 10U);
    }
    value.globalPowerLimit = 80.0f;
    value.timeValid = true;
    value.wifiState = Network::NetworkState::Connected;
    value.wifiConnected = true;
    value.wifiIp = {{192U, 168U, 1U, 55U}};
    value.wifiRssi = -58;
    value.overallHealth = Diagnostics::HealthState::Ok;
    return value;
}

void test_route_inventory_and_post_limits() {
    TEST_ASSERT_TRUE(fixture->start());
    TEST_ASSERT_EQUAL_UINT32(11U, fixture->transport.count);
    const char* gets[] = {"/", "/assets/aqua.css", "/api/system",
        "/api/diagnostics", "/control", "/diagnostics", "/system",
        "/api/lumasense/status"};
    for (const char* path : gets) {
        const Transport::Route* route = fixture->transport.find(path, HttpMethod::Get);
        TEST_ASSERT_NOT_NULL(route);
        TEST_ASSERT_EQUAL_UINT32(0U, route->options.maxBodyLength);
    }
    const char* posts[] = {"/api/lumasense/mode", "/api/lumasense/profile",
                           "/api/lumasense/manual"};
    for (const char* path : posts) {
        const Transport::Route* route = fixture->transport.find(path, HttpMethod::Post);
        TEST_ASSERT_NOT_NULL(route);
        TEST_ASSERT_EQUAL_UINT32(512U, route->options.maxBodyLength);
    }
}

void test_status_is_snapshot_compatible_and_secret_free() {
    TEST_ASSERT_TRUE(fixture->start());
    TEST_ASSERT_TRUE(fixture->statusSnapshot.publish(statusValue(1U, "Main \"Day\"")));
    Writer first = fixture->transport.request("/api/lumasense/status");
    TEST_ASSERT_EQUAL_UINT16(200U, first.status);
    assertContains(first.text(), "\"mode\":\"NORMAL\"");
    assertContains(first.text(), "\"activeProfile\":1");
    assertContains(first.text(), "\"activeProfileName\":\"Main \\\"Day\\\"\"");
    assertContains(first.text(), "\"dayState\":\"NIGHT\"");
    assertContains(first.text(), "\"localTime\":\"2026-09-10 10:30:05\"");
    assertContains(first.text(), "\"requestedLevels\":[1.00,2.00,3.00,4.00,5.00,6.00,7.00,8.00]");
    assertContains(first.text(), "\"finalLevels\":[10.00,20.00,30.00,40.00,50.00,60.00,70.00,80.00]");
    assertContains(first.text(), "\"globalPowerLimit\":80.00");
    assertContains(first.text(), "\"state\":\"connected\"");
    assertContains(first.text(), "\"ip\":\"192.168.1.55\"");
    assertContains(first.text(), "\"rssi\":-58");
    assertContains(first.text(), "\"overallHealth\":\"ok\"");
    assertMissing(first.text(), "password");
    assertMissing(first.text(), "ssid");

    LumaStatusProjection changed = statusValue(2U, "Second");
    Writer stale = fixture->transport.request("/api/lumasense/status");
    assertContains(stale.text(), "\"activeProfile\":1");
    TEST_ASSERT_TRUE(fixture->statusSnapshot.publish(changed));
    Writer fresh = fixture->transport.request("/api/lumasense/status");
    assertContains(fresh.text(), "\"activeProfile\":2");
}

void test_pages_and_core_builtins_use_shared_native_service() {
    TEST_ASSERT_TRUE(fixture->start());
    TEST_ASSERT_TRUE(fixture->statusSnapshot.publish(statusValue(1U, "Main")));
    const char* pages[] = {"/", "/control", "/diagnostics", "/system"};
    for (const char* path : pages) {
        Writer response = fixture->transport.request(path);
        TEST_ASSERT_EQUAL_UINT16(200U, response.status);
        assertContains(response.text(), "/assets/aqua.css");
    }
    Writer dashboard = fixture->transport.request("/");
    assertContains(dashboard.text(), "setInterval(refresh,1500)");
    Writer control = fixture->transport.request("/control");
    assertContains(control.text(), "body.ok===true");
    Writer system = fixture->transport.request("/api/system");
    TEST_ASSERT_EQUAL_UINT16(200U, system.status);
    assertContains(system.text(), "\"deviceType\":\"lighting-controller\"");
    assertContains(system.text(), "\"device_type\":\"luma\"");
    assertContains(system.text(), "\"device_id\":\"246F28A1B2C3\"");
    TEST_ASSERT_TRUE(hasApiProtocolVersion(system.text(), 1, 0));
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->transport.request("/api/diagnostics").status);
    TEST_ASSERT_EQUAL_UINT16(404U, fixture->transport.request("/missing").status);
}

void test_application_side_exact_results_and_authority_isolation() {
    std::unique_ptr<AuthorityFixture> ownedAuthority(
        new (std::nothrow) AuthorityFixture());
    if (!ownedAuthority) { TEST_FAIL_MESSAGE("AuthorityFixture allocation failed"); return; }
    AuthorityFixture& authority = *ownedAuthority;
    TEST_ASSERT_TRUE(authority.start());

    LumaWebRequest service {};
    service.kind = LumaWebRequestKind::SetMode;
    service.mode = OperatingMode::Service;
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      authority.bridge.submit(service, token));
    TEST_ASSERT_EQUAL(OperatingMode::Normal, authority.app.state().mode);
    TEST_ASSERT_TRUE(authority.webApplication.processOne(1234U));
    FirmwareCommandResult result = FirmwareCommandResult::Rejected;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::Completed,
                      authority.bridge.wait(token, 0U, result));
    TEST_ASSERT_EQUAL(FirmwareCommandResult::Applied, result);
    TEST_ASSERT_EQUAL(OperatingMode::Service, authority.app.state().mode);

    TEST_ASSERT_EQUAL(FirmwareCommandResult::NoChange,
                      authority.execute(service, 1500U));

    LumaWebRequest manual {};
    manual.kind = LumaWebRequestKind::SetManual;
    manual.timeoutMinutes = 15U;
    for (uint8_t i = 0U; i < CHANNEL_COUNT; ++i)
        manual.levels.value[i] = static_cast<float>(i * 10U);
    TEST_ASSERT_EQUAL(FirmwareCommandResult::Applied,
                      authority.execute(manual, 9000U));
    TEST_ASSERT_EQUAL(OperatingMode::Manual, authority.app.state().mode);
    TEST_ASSERT_TRUE(authority.app.update(908999U));
    TEST_ASSERT_EQUAL(OperatingMode::Manual, authority.app.state().mode);
    TEST_ASSERT_TRUE(authority.app.update(909000U));
    TEST_ASSERT_EQUAL(OperatingMode::Service, authority.app.state().mode);

    manual.timeoutMinutes = 0U;
    TEST_ASSERT_EQUAL(FirmwareCommandResult::Applied,
                      authority.execute(manual, 910000U));
    LumaWebRequest exitManual {};
    exitManual.kind = LumaWebRequestKind::ExitManual;
    TEST_ASSERT_EQUAL(FirmwareCommandResult::Applied,
                      authority.execute(exitManual, 910001U));
    TEST_ASSERT_EQUAL(OperatingMode::Service, authority.app.state().mode);

    LumaWebRequest profile {};
    profile.kind = LumaWebRequestKind::SelectProfile;
    profile.profileIndex = 2U;
    Preferences::failNextWrite();
    TEST_ASSERT_EQUAL(FirmwareCommandResult::StorageFailure,
                      authority.execute(profile, 9100U));
    TEST_ASSERT_EQUAL_UINT8(0U, authority.app.config().activeProfileIndex);
    TEST_ASSERT_EQUAL(FirmwareCommandResult::Applied,
                      authority.execute(profile, 9200U));
    TEST_ASSERT_EQUAL_UINT8(2U, authority.app.config().activeProfileIndex);
    TEST_ASSERT_TRUE(authority.webApplication.publishStatus());
    LumaStatusProjection published {};
    TEST_ASSERT_TRUE(authority.status.read(published));
    TEST_ASSERT_EQUAL_UINT8(3U, published.activeProfile);
    TEST_ASSERT_TRUE(authority.app.begin(0U));
    TEST_ASSERT_EQUAL_UINT8(2U, authority.app.config().activeProfileIndex);

    TEST_ASSERT_TRUE(authority.webApplication.publishStatus());
    authority.hardware.ready = false;
    TEST_ASSERT_FALSE(authority.app.update(9300U));
    TEST_ASSERT_TRUE(authority.webApplication.publishStatus());
    TEST_ASSERT_FALSE(authority.status.read(published));
}

void test_strict_parsers_reject_invalid_and_trailing_input() {
    LumaWebRequest request {};
    const char* error = nullptr;
    const char* service = "{\"mode\":\"SERVICE\"}";
    TEST_ASSERT_TRUE(parseModeRequest(service, std::strlen(service), request, error));
    TEST_ASSERT_EQUAL(LumaWebRequestKind::SetMode, request.kind);
    TEST_ASSERT_EQUAL(OperatingMode::Service, request.mode);
    const char* exitManual = "{\"mode\":\"EXIT_MANUAL\"}";
    TEST_ASSERT_TRUE(parseModeRequest(
        exitManual, std::strlen(exitManual), request, error
    ));
    TEST_ASSERT_EQUAL(LumaWebRequestKind::ExitManual, request.kind);
    const char* trailing = "{\"mode\":\"SERVICE\"}x";
    TEST_ASSERT_FALSE(parseModeRequest(
        trailing, std::strlen(trailing), request, error
    ));
    const char embeddedNul[] =
        {'{','\"','m','o','d','e','\"',':','\"','O','F','F','\"','}',0,'x'};
    TEST_ASSERT_FALSE(parseModeRequest(
        embeddedNul, sizeof(embeddedNul), request, error
    ));

    const char* profileOne = "{\"profile\":1}";
    TEST_ASSERT_TRUE(parseProfileRequest(
        profileOne, std::strlen(profileOne), request, error
    ));
    TEST_ASSERT_EQUAL_UINT8(0U, request.profileIndex);
    const char* profileFive = "{\"profile\":5}";
    TEST_ASSERT_TRUE(parseProfileRequest(
        profileFive, std::strlen(profileFive), request, error
    ));
    TEST_ASSERT_EQUAL_UINT8(4U, request.profileIndex);
    const char* invalidProfiles[] = {"{\"profile\":0}", "{\"profile\":6}"};
    for (const char* body : invalidProfiles)
        TEST_ASSERT_FALSE(parseProfileRequest(body, std::strlen(body), request, error));

    const char* manual =
        "{\"levels\":[0,1,2,3,4,5,99,100],\"timeoutMinutes\":60}";
    TEST_ASSERT_TRUE(parseManualRequest(
        manual, std::strlen(manual), request, error
    ));
    TEST_ASSERT_EQUAL(LumaWebRequestKind::SetManual, request.kind);
    TEST_ASSERT_EQUAL_UINT16(60U, request.timeoutMinutes);
    const char* invalidManual[] = {
        "{\"levels\":[-1,0,0,0,0,0,0,0],\"timeoutMinutes\":0}",
        "{\"levels\":[101,0,0,0,0,0,0,0],\"timeoutMinutes\":0}",
        "{\"levels\":[1,2,3,4,5,6,7],\"timeoutMinutes\":0}",
        "{\"levels\":[1,2,3,4,5,6,7,8],\"timeoutMinutes\":5}",
        "{\"levels\":[1,2,3,4,5,6,7,8],\"timeoutMinutes\":0}x"
    };
    for (const char* body : invalidManual)
        TEST_ASSERT_FALSE(parseManualRequest(body, std::strlen(body), request, error));
}

FirmwareCommandResult countExecution(const LumaWebRequest&, void* context) {
    ++*static_cast<uint32_t*>(context);
    return FirmwareCommandResult::Applied;
}

struct RouteExecution {
    FirmwareCommandResult result = FirmwareCommandResult::Applied;
    LumaWebRequest request {};
    uint32_t calls = 0U;
};

FirmwareCommandResult captureExecution(
    const LumaWebRequest& request, void* context
) {
    RouteExecution& execution = *static_cast<RouteExecution*>(context);
    execution.request = request;
    ++execution.calls;
    return execution.result;
}

void test_native_action_routes_preserve_typed_requests_and_exact_results() {
    TEST_ASSERT_TRUE(fixture->start());
    RouteExecution execution {};
    fixture->actionSync.executor = captureExecution;
    fixture->actionSync.executorContext = &execution;

    Writer applied = fixture->transport.request(
        "/api/lumasense/mode", HttpMethod::Post, "{\"mode\":\"SERVICE\"}"
    );
    TEST_ASSERT_EQUAL_UINT16(200U, applied.status);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"result\":\"applied\"}", applied.text());
    TEST_ASSERT_EQUAL(LumaWebRequestKind::SetMode, execution.request.kind);
    TEST_ASSERT_EQUAL(OperatingMode::Service, execution.request.mode);

    execution.result = FirmwareCommandResult::NoChange;
    Writer noChange = fixture->transport.request(
        "/api/lumasense/profile", HttpMethod::Post, "{\"profile\":3}"
    );
    TEST_ASSERT_EQUAL_UINT16(200U, noChange.status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"result\":\"no_change\"}", noChange.text()
    );
    TEST_ASSERT_EQUAL(LumaWebRequestKind::SelectProfile, execution.request.kind);
    TEST_ASSERT_EQUAL_UINT8(2U, execution.request.profileIndex);

    execution.result = FirmwareCommandResult::StorageFailure;
    Writer storage = fixture->transport.request(
        "/api/lumasense/profile", HttpMethod::Post, "{\"profile\":4}"
    );
    TEST_ASSERT_EQUAL_UINT16(409U, storage.status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":false,\"error\":\"storage unavailable\"}", storage.text()
    );

    execution.result = FirmwareCommandResult::Rejected;
    Writer rejected = fixture->transport.request(
        "/api/lumasense/manual", HttpMethod::Post,
        "{\"levels\":[0,1,2,3,4,5,6,7],\"timeoutMinutes\":30}"
    );
    TEST_ASSERT_EQUAL_UINT16(409U, rejected.status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":false,\"error\":\"state conflict\"}", rejected.text()
    );
    TEST_ASSERT_EQUAL(LumaWebRequestKind::SetManual, execution.request.kind);
    TEST_ASSERT_EQUAL_UINT16(30U, execution.request.timeoutMinutes);
    TEST_ASSERT_EQUAL_UINT32(4U, execution.calls);
}

void test_malformed_timeout_and_late_single_execution() {
    TEST_ASSERT_TRUE(fixture->start());
    Writer malformed = fixture->transport.request(
        "/api/lumasense/manual", HttpMethod::Post,
        "{\"levels\":[1,2],\"timeoutMinutes\":0}"
    );
    TEST_ASSERT_EQUAL_UINT16(400U, malformed.status);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture->bridge.pendingCount());

    Writer timeout = fixture->transport.request(
        "/api/lumasense/mode", HttpMethod::Post, "{\"mode\":\"SERVICE\"}"
    );
    TEST_ASSERT_EQUAL_UINT16(202U, timeout.status);
    assertContains(timeout.text(), "\"ok\":false");
    assertContains(timeout.text(), "outcome_unknown");
    uint32_t executions = 0U;
    TEST_ASSERT_TRUE(fixture->bridge.processOne(countExecution, &executions));
    TEST_ASSERT_EQUAL_UINT32(1U, executions);
    TEST_ASSERT_FALSE(fixture->bridge.processOne(countExecution, &executions));
    TEST_ASSERT_EQUAL_UINT32(1U, executions);
}

void test_queue_full_is_not_accepted() {
    TEST_ASSERT_TRUE(fixture->start());
    ApplicationBridgeToken tokens[4] {};
    for (size_t i = 0U; i < 4U; ++i) {
        LumaWebRequest request {};
        request.kind = LumaWebRequestKind::SetMode;
        request.mode = OperatingMode::Service;
        TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                          fixture->bridge.submit(request, tokens[i]));
    }
    Writer response = fixture->transport.request(
        "/api/lumasense/mode", HttpMethod::Post, "{\"mode\":\"OFF\"}"
    );
    TEST_ASSERT_EQUAL_UINT16(503U, response.status);
    assertContains(response.text(), "service unavailable");
}

void test_web_registration_and_begin_failure_preserve_application_autonomy() {
    std::unique_ptr<AuthorityFixture> ownedAuthority(
        new (std::nothrow) AuthorityFixture());
    if (!ownedAuthority) { TEST_FAIL_MESSAGE("AuthorityFixture allocation failed"); return; }
    AuthorityFixture& authority = *ownedAuthority;
    TEST_ASSERT_TRUE(authority.start());

    std::unique_ptr<Fixture> ownedRegistrationFailure(new (std::nothrow) Fixture());
    if (!ownedRegistrationFailure) { TEST_FAIL_MESSAGE("Fixture allocation failed"); return; }
    Fixture& registrationFailure = *ownedRegistrationFailure;
    registrationFailure.transport.rejectNextRoute = true;
    TEST_ASSERT_FALSE(registrationFailure.luma.registerRoutes());
    TEST_ASSERT_TRUE(authority.app.update(1000U));
    TEST_ASSERT_TRUE(authority.app.isRunning());

    std::unique_ptr<Fixture> ownedBeginFailure(new (std::nothrow) Fixture());
    if (!ownedBeginFailure) { TEST_FAIL_MESSAGE("Fixture allocation failed"); return; }
    Fixture& beginFailure = *ownedBeginFailure;
    TEST_ASSERT_TRUE(beginFailure.luma.registerRoutes());
    beginFailure.transport.beginResult = false;
    TEST_ASSERT_FALSE(beginFailure.native.begin(webConfig()));
    TEST_ASSERT_TRUE(authority.app.update(2000U));
    TEST_ASSERT_TRUE(authority.app.isRunning());

    LumaWebRequest service {};
    service.kind = LumaWebRequestKind::SetMode;
    service.mode = OperatingMode::Service;
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL(ApplicationBridgeSubmitResult::Accepted,
                      authority.bridge.submit(service, token));
    FirmwareCommandResult result = FirmwareCommandResult::Rejected;
    TEST_ASSERT_EQUAL(ApplicationBridgeWaitResult::TimedOutAccepted,
                      authority.bridge.wait(token, 0U, result));
    TEST_ASSERT_TRUE(authority.app.update(3000U));
    TEST_ASSERT_TRUE(authority.webApplication.processOne(3000U));
    TEST_ASSERT_EQUAL(OperatingMode::Service, authority.app.state().mode);
    TEST_ASSERT_TRUE(authority.app.isRunning());
}

} // namespace

void setUp() {
    Preferences::reset();
    Wire.reset();
    fillRtc();
    fixture = new Fixture();
    TEST_ASSERT_NOT_NULL(fixture);
}
void tearDown() { delete fixture; fixture = nullptr; }

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_route_inventory_and_post_limits);
    RUN_TEST(test_status_is_snapshot_compatible_and_secret_free);
    RUN_TEST(test_pages_and_core_builtins_use_shared_native_service);
    RUN_TEST(test_application_side_exact_results_and_authority_isolation);
    RUN_TEST(test_strict_parsers_reject_invalid_and_trailing_input);
    RUN_TEST(test_native_action_routes_preserve_typed_requests_and_exact_results);
    RUN_TEST(test_malformed_timeout_and_late_single_execution);
    RUN_TEST(test_queue_full_is_not_accepted);
    RUN_TEST(test_web_registration_and_begin_failure_preserve_application_autonomy);
    UNITY_END();
}
void loop() {}
