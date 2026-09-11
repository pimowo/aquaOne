#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "Preferences.h"
#include "Wire.h"

#define LUMASENSE_STORAGE_PREFERENCES_HEADER "../../test/test_ac8/Preferences.h"
#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_ac8/Wire.h"

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
#include "../../src/web/LumaWebApp.cpp"

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/System/SystemBackend.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/WebBackend.h"
#include "AquaCore/Web/WebService.h"

using namespace LumaSense;
using namespace AquaCore;
using namespace AquaCore::Network;
using namespace AquaCore::Web;

namespace {

uint8_t bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}
void fillRtc() {
    Wire.reg(0x00) = bcd(5U);
    Wire.reg(0x01) = bcd(30U);
    Wire.reg(0x02) = bcd(10U);
    Wire.reg(0x03) = bcd(1U);
    Wire.reg(0x04) = bcd(10U);
    Wire.reg(0x05) = bcd(9U);
    Wire.reg(0x06) = bcd(26U);
    Wire.reg(0x0F) = 0U;
}

class MockHardware final : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override {
        ++beginCalls;
        ready = beginResult && channels != nullptr;
        return ready;
    }
    bool setChannelPercent(uint8_t channel, float percent) override {
        ++setCalls;
        if (!ready || channel >= CHANNEL_COUNT) return false;
        last[channel] = percent;
        return true;
    }
    bool allChannelsOff() override {
        ++offCalls;
        for (float& value : last) value = 0.0f;
        return ready;
    }
    bool isReady() const override { return ready; }
    uint8_t availableChannelCount() const override { return CHANNEL_COUNT; }
    bool isChannelAvailable(uint8_t channel) const override {
        return channel < CHANNEL_COUNT;
    }
    bool beginResult = true;
    bool ready = false;
    uint16_t beginCalls = 0U, offCalls = 0U;
    uint32_t setCalls = 0U;
    float last[CHANNEL_COUNT] {};
};

class SysBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override { return nowMs; }
    RestartReason restartReason() const override { return RestartReason::PowerOn; }
    uint32_t nowMs = 0U;
};

class NetBackend final : public NetworkBackend {
public:
    bool setHostname(const char*) override { ++hostnameCalls; return true; }
    bool beginSta(const char*, const char*) override {
        ++beginCalls; return beginResult;
    }
    BackendStaState staState() const override { return state; }
    bool reconnectSta() override { ++reconnectCalls; return reconnectResult; }
    bool disconnectSta() override { ++disconnectCalls; return true; }
    IpAddress localIp() const override { return {{192U, 168U, 1U, 55U}}; }
    int32_t rssi() const override { return -58; }
    bool startAccessPoint(const char*, const char*) override {
        ++apStartCalls; return true;
    }
    bool stopAccessPoint() override { ++apStopCalls; return true; }
    IpAddress accessPointIp() const override { return {{192U, 168U, 4U, 1U}}; }

    BackendStaState state = BackendStaState::Connected;
    bool beginResult = true, reconnectResult = true;
    uint16_t hostnameCalls = 0U, beginCalls = 0U, reconnectCalls = 0U;
    uint16_t disconnectCalls = 0U, apStartCalls = 0U, apStopCalls = 0U;
};

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t value, ContentType type) override {
        if (begun) return false;
        begun = true; status = value; contentType = type; return true;
    }
    bool write(const char* data, size_t length) override {
        if (!begun || ended || !data || size + length >= sizeof(body)) return false;
        memcpy(body + size, data, length); size += length; body[size] = '\0';
        return true;
    }
    bool endResponse() override {
        if (!begun || ended) return false;
        ended = true; return true;
    }
    void clear() {
        begun = false; ended = false; status = 0U; size = 0U; body[0] = '\0';
    }
    bool begun = false, ended = false;
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    char body[24576] {};
    size_t size = 0U;
};

class HttpBackend final : public WebBackend {
public:
    struct Route {
        char path[64] {};
        HttpMethod method = HttpMethod::Get;
        WebRouteHandler handler = nullptr;
        void* context = nullptr;
    };
    bool addRoute(const char* path, HttpMethod method,
                  WebRouteHandler handler, void* context) override {
        if (!path || !handler || count >= 16U) return false;
        for (size_t i = 0U; i < count; ++i)
            if (routes[i].method == method && strcmp(routes[i].path, path) == 0)
                return false;
        strncpy(routes[count].path, path, 63U);
        routes[count].method = method;
        routes[count].handler = handler;
        routes[count].context = context;
        ++count; return true;
    }
    bool setNotFoundHandler(WebRouteHandler handler, void* context) override {
        notFound = handler; notFoundContext = context; return handler != nullptr;
    }
    bool begin(uint16_t) override { running = beginResult; return running; }
    void update() override { ++updateCalls; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }
    void request(const char* path, HttpMethod method = HttpMethod::Get,
                 const char* body = nullptr) {
        output.clear();
        WebRequest req {method, path, body, body ? strlen(body) : 0U};
        for (size_t i = 0U; i < count; ++i) {
            if (routes[i].method == method && strcmp(routes[i].path, path) == 0) {
                routes[i].handler(routes[i].context, req, output); return;
            }
        }
        if (notFound) notFound(notFoundContext, req, output);
    }
    Route routes[16] {};
    size_t count = 0U;
    WebRouteHandler notFound = nullptr;
    void* notFoundContext = nullptr;
    Writer output;
    bool beginResult = true, running = false;
    uint16_t updateCalls = 0U;
};

NetworkConfig networkConfig() {
    NetworkConfig config {};
    config.staEnabled = true;
    strcpy(config.ssid, "AquaLab");
    strcpy(config.password, "wifi-password-ac8");
    strcpy(config.hostname, "lumasense");
    config.autoReconnect = true;
    config.reconnectIntervalMs = 10000U;
    return config;
}
AquaCore::Web::WebConfig webConfig() {
    AquaCore::Web::WebConfig config {};
    config.enabled = true;
    config.navigationMask =
        navigationSectionMask(NavigationSection::Dashboard) |
        navigationSectionMask(NavigationSection::Control) |
        navigationSectionMask(NavigationSection::Diagnostics) |
        navigationSectionMask(NavigationSection::System);
    return config;
}

struct Fixture {
    Fixture()
        : app(hardware, storage, time),
          system(sysBackend),
          network(netBackend),
          diagnostics(system, time.rtcService(), storage.aquaService(),
                      "Europe/Warsaw", nullptr, &network),
          web(httpBackend, system, &diagnostics),
          lumaWeb(web, app, network, diagnostics) {}

    bool start(const char* name = "LumaSense") {
        if (!system.begin(DeviceIdentity(
            "lighting-controller", name, "0.2.0", "LOLIN32_TEST"))) return false;
        if (!app.begin(0U)) return false;
        if (!network.begin(networkConfig())) return false;
        network.update(10U);
        if (!lumaWeb.registerRoutes()) return false;
        lumaWeb.update(100U);
        return web.begin(webConfig());
    }

    MockHardware hardware;
    StorageService storage;
    TimeService time;
    FirmwareApp app;
    SysBackend sysBackend;
    SystemService system;
    NetBackend netBackend;
    NetworkService network;
    AquaCore::Diagnostics::DiagnosticsService diagnostics;
    HttpBackend httpBackend;
    AquaCore::Web::WebService web;
    LumaSense::Web::LumaWebApp lumaWeb;
};

Fixture* fixture = nullptr;

void assertContains(const char* text, const char* part) {
    TEST_ASSERT_NOT_NULL(strstr(text, part));
}
void assertMissing(const char* text, const char* part) {
    TEST_ASSERT_NULL(strstr(text, part));
}
size_t countOccurrences(const char* text, const char* part) {
    if (!text || !part || part[0] == '\0') return 0U;
    size_t count = 0U;
    const size_t length = strlen(part);
    const char* cursor = text;
    while ((cursor = strstr(cursor, part)) != nullptr) {
        ++count;
        cursor += length;
    }
    return count;
}
void assertOccursExactly(const char* text, const char* part, size_t expected) {
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(expected),
        static_cast<uint32_t>(countOccurrences(text, part))
    );
}
void get(const char* path) {
    fixture->httpBackend.request(path);
}
void post(const char* path, const char* body) {
    fixture->httpBackend.request(path, HttpMethod::Post, body);
}
void assertMode(OperatingMode expected) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(fixture->app.state().mode)
    );
}
void seedProfileName(const char* name) {
    DeviceConfig config = createDefaultConfig();
    strncpy(config.profiles[0].name, name,
            sizeof(config.profiles[0].name) - 1U);
    StorageService seed;
    TEST_ASSERT_TRUE(seed.begin());
    TEST_ASSERT_TRUE(seed.save(config));
}

void test_status_endpoint_works() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
}
void test_status_contains_mode() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    assertContains(fixture->httpBackend.output.body, "\"mode\":\"NORMAL\"");
}
void test_status_contains_active_profile() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    assertContains(fixture->httpBackend.output.body, "\"activeProfile\":1");
}
void test_status_contains_day_state() {
    TEST_ASSERT_TRUE(fixture->start()); fixture->app.update(0U);
    get("/api/lumasense/status");
    assertContains(fixture->httpBackend.output.body, "\"dayState\":\"DAY\"");
}
void test_status_contains_eight_channels() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    assertContains(fixture->httpBackend.output.body,
        "\"finalLevels\":[0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00]");
    assertContains(fixture->httpBackend.output.body, "\"requestedLevels\":[");
}
void test_status_has_no_secrets() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    assertMissing(fixture->httpBackend.output.body, "wifi-password-ac8");
    assertMissing(fixture->httpBackend.output.body, "\"password\"");
}
void test_valid_profile_command() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/profile", "{\"profile\":3}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    TEST_ASSERT_EQUAL_UINT8(2U, fixture->app.config().activeProfileIndex);
}
void test_invalid_profile_rejected() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/profile", "{\"profile\":6}");
    TEST_ASSERT_EQUAL_UINT16(400U, fixture->httpBackend.output.status);
    TEST_ASSERT_EQUAL_UINT8(0U, fixture->app.config().activeProfileIndex);
}
void test_normal_command() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"SERVICE\"}");
    post("/api/lumasense/mode", "{\"mode\":\"NORMAL\"}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Normal);
}
void test_off_command() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"OFF\"}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Off);
}
void test_service_command() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"SERVICE\"}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Service);
}
void test_manual_command() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[10,20,30,40,50,60,70,80],\"timeoutMinutes\":0}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Manual);
}
void test_manual_values_go_through_core() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[11,22,33,44,55,66,77,88],\"timeoutMinutes\":15}");
    fixture->app.update(100U);
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 44.0f, fixture->app.state().requestedLevels.value[3]);
}
void test_manual_below_zero_rejected() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[-1,0,0,0,0,0,0,0],\"timeoutMinutes\":0}");
    TEST_ASSERT_EQUAL_UINT16(400U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Normal);
}
void test_manual_above_100_rejected() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[101,0,0,0,0,0,0,0],\"timeoutMinutes\":0}");
    TEST_ASSERT_EQUAL_UINT16(400U, fixture->httpBackend.output.status);
}
void test_invalid_manual_timeout_rejected() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[1,2,3,4,5,6,7,8],\"timeoutMinutes\":5}");
    TEST_ASSERT_EQUAL_UINT16(400U, fixture->httpBackend.output.status);
}
void test_web_does_not_call_hardware_directly() {
    TEST_ASSERT_TRUE(fixture->start());
    const uint32_t before = fixture->hardware.setCalls;
    get("/api/lumasense/status");
    TEST_ASSERT_EQUAL_UINT32(before, fixture->hardware.setCalls);
}
void test_network_loss_does_not_stop_core() {
    TEST_ASSERT_TRUE(fixture->start());
    fixture->netBackend.state = BackendStaState::Disconnected;
    fixture->network.update(1000U);
    TEST_ASSERT_TRUE(fixture->app.update(1000U));
    TEST_ASSERT_TRUE(fixture->app.isRunning());
}
void test_reconnect_does_not_reset_mode() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"SERVICE\"}");
    fixture->netBackend.state = BackendStaState::Disconnected;
    fixture->network.update(1000U);
    fixture->network.update(11000U);
    assertMode(OperatingMode::Service);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture->netBackend.reconnectCalls);
}
void test_web_failure_does_not_stop_core() {
    fixture->httpBackend.beginResult = false;
    TEST_ASSERT_TRUE(fixture->system.begin(DeviceIdentity(
        "lighting-controller", "LumaSense", "0.2.0", "TEST")));
    TEST_ASSERT_TRUE(fixture->app.begin(0U));
    TEST_ASSERT_TRUE(fixture->network.begin(networkConfig()));
    TEST_ASSERT_TRUE(fixture->lumaWeb.registerRoutes());
    TEST_ASSERT_FALSE(fixture->web.begin(webConfig()));
    TEST_ASSERT_TRUE(fixture->app.update(1U));
}
void test_restart_does_not_restore_manual_or_off() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/manual",
         "{\"levels\":[1,2,3,4,5,6,7,8],\"timeoutMinutes\":0}");
    assertMode(OperatingMode::Manual);
    TEST_ASSERT_TRUE(fixture->app.begin(0U));
    assertMode(OperatingMode::Normal);
    fixture->app.commandMode(OperatingMode::Off);
    assertMode(OperatingMode::Off);
    TEST_ASSERT_TRUE(fixture->app.begin(0U));
    assertMode(OperatingMode::Normal);
}
void test_consecutive_requests_do_not_corrupt_state() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"SERVICE\"}");
    get("/api/lumasense/status");
    assertMode(OperatingMode::Service);
    assertContains(fixture->httpBackend.output.body, "\"mode\":\"SERVICE\"");
}
void test_malformed_post_returns_400() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{bad");
    TEST_ASSERT_EQUAL_UINT16(400U, fixture->httpBackend.output.status);
}
void test_unknown_endpoint_returns_404() {
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/unknown");
    TEST_ASSERT_EQUAL_UINT16(404U, fixture->httpBackend.output.status);
}
void test_status_json_escapes_profile_name() {
    seedProfileName("Main \"Day\"\nA");
    TEST_ASSERT_TRUE(fixture->start()); get("/api/lumasense/status");
    assertContains(fixture->httpBackend.output.body,
                   "\"activeProfileName\":\"Main \\\"Day\\\"\\nA\"");
}
void test_dashboard_renders_identity() {
    TEST_ASSERT_TRUE(fixture->start("Luma <One>")); get("/");
    assertContains(fixture->httpBackend.output.body, "<h1>Luma &lt;One&gt;</h1>");
}
void test_dashboard_uses_aqua_shell() {
    TEST_ASSERT_TRUE(fixture->start()); get("/");
    assertContains(fixture->httpBackend.output.body, "/assets/aqua.css");
    assertContains(fixture->httpBackend.output.body, "Aqua Core");
}
void test_dashboard_uses_1500ms_polling() {
    TEST_ASSERT_TRUE(fixture->start()); get("/");
    assertContains(fixture->httpBackend.output.body, "setInterval(refresh,1500)");
}
void test_polling_does_not_change_state() {
    TEST_ASSERT_TRUE(fixture->start());
    const RuntimeState before = fixture->app.state();
    get("/api/lumasense/status"); get("/api/lumasense/status");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)before.mode, (uint8_t)fixture->app.state().mode);
    TEST_ASSERT_EQUAL_UINT8(before.currentStageIndex,
                           fixture->app.state().currentStageIndex);
}
void test_system_and_diagnostics_apis_still_work() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/api/system"); TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertContains(fixture->httpBackend.output.body, "\"deviceName\":\"LumaSense\"");
    get("/api/diagnostics"); TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertContains(fixture->httpBackend.output.body, "\"network\":");
}
void test_no_password_in_any_page_or_api() {
    TEST_ASSERT_TRUE(fixture->start());
    const char* paths[] = {"/", "/control", "/diagnostics", "/system",
                           "/api/system", "/api/diagnostics",
                           "/api/lumasense/status"};
    for (const char* path : paths) {
        get(path);
        assertMissing(fixture->httpBackend.output.body, "wifi-password-ac8");
        assertMissing(fixture->httpBackend.output.body, "apPassword");
    }
}
void test_dashboard_uses_panel_rows_and_both_channel_levels() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/");
    assertContains(fixture->httpBackend.output.body, "class=\"row\"");
    assertContains(fixture->httpBackend.output.body, "id=\"req0\"");
    assertContains(fixture->httpBackend.output.body, "id=\"req7\"");
    assertContains(fixture->httpBackend.output.body, "id=\"ch7\"");
    assertContains(fixture->httpBackend.output.body, "</html>");
}
void test_dashboard_has_exactly_one_row_for_each_channel() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/");
    const char* html = fixture->httpBackend.output.body;
    assertOccursExactly(html, "data-dashboard-section=\"channels\"", 1U);
    assertOccursExactly(html, "<h2>Kana&#322;y</h2>", 1U);
    const char* channelRows[] = {
        "data-channel=\"1\"", "data-channel=\"2\"",
        "data-channel=\"3\"", "data-channel=\"4\"",
        "data-channel=\"5\"", "data-channel=\"6\"",
        "data-channel=\"7\"", "data-channel=\"8\""
    };
    for (const char* row : channelRows) {
        assertOccursExactly(html, row, 1U);
    }
    assertMissing(html, "data-channel=\"9\"");
}
void test_navigation_renders_each_lumasense_item_once() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/");
    assertOccursExactly(fixture->httpBackend.output.body, ">Dashboard</a>", 1U);
    assertOccursExactly(fixture->httpBackend.output.body, ">Sterowanie</a>", 1U);
    assertOccursExactly(fixture->httpBackend.output.body, ">Diagnostyka</a>", 1U);
    assertOccursExactly(fixture->httpBackend.output.body, ">System</a>", 1U);
    assertMissing(fixture->httpBackend.output.body, ">Automation</a>");
    assertMissing(fixture->httpBackend.output.body, ">Settings</a>");
}
void test_control_renders_one_manual_action_set() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/control");
    assertOccursExactly(fixture->httpBackend.output.body, "id=\"manual\"", 1U);
    assertOccursExactly(fixture->httpBackend.output.body, "id=\"exitManual\"", 1U);
}
void test_system_renders_restart_reason_once() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/system");
    assertOccursExactly(fixture->httpBackend.output.body, "id=\"restart\"", 1U);
    assertOccursExactly(fixture->httpBackend.output.body, "v.restartReason", 1U);

    get("/api/system");
    assertOccursExactly(
        fixture->httpBackend.output.body,
        "\"restartReason\":\"POWER_ON\"",
        1U
    );
    assertOccursExactly(fixture->httpBackend.output.body, "POWER_ON", 1U);
}

void test_control_keeps_all_timeout_choices_and_active_state() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/control");
    assertContains(fixture->httpBackend.output.body, "id=\"timeout\"");
    assertContains(fixture->httpBackend.output.body, "value=\"15\">15 minut");
    assertContains(fixture->httpBackend.output.body, "value=\"30\">30 minut");
    assertContains(fixture->httpBackend.output.body, "value=\"60\">60 minut");
    assertContains(fixture->httpBackend.output.body, "timeoutMinutes:Number");
    assertContains(
        fixture->httpBackend.output.body,
        "<a class=\"active\" aria-current=\"page\" href=\"/control\">"
    );
    assertContains(fixture->httpBackend.output.body, "</html>");
}
void test_diagnostics_and_system_use_structured_cards() {
    TEST_ASSERT_TRUE(fixture->start());
    get("/diagnostics");
    assertContains(fixture->httpBackend.output.body, "id=\"timeHealth\"");
    assertContains(fixture->httpBackend.output.body, "id=\"storageHealth\"");
    assertContains(fixture->httpBackend.output.body, "id=\"reconnects\"");
    assertMissing(fixture->httpBackend.output.body, "<pre");
    assertContains(fixture->httpBackend.output.body, "</html>");

    get("/system");
    assertContains(fixture->httpBackend.output.body, "id=\"deviceName\"");
    assertContains(fixture->httpBackend.output.body, "id=\"firmware\"");
    assertContains(fixture->httpBackend.output.body, "id=\"restart\"");
    assertMissing(fixture->httpBackend.output.body, "<pre");
    assertContains(fixture->httpBackend.output.body, "</html>");
}

void test_exit_manual_returns_to_previous_mode() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/mode", "{\"mode\":\"SERVICE\"}");
    post("/api/lumasense/manual",
         "{\"levels\":[1,2,3,4,5,6,7,8],\"timeoutMinutes\":0}");
    post("/api/lumasense/mode", "{\"mode\":\"EXIT_MANUAL\"}");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture->httpBackend.output.status);
    assertMode(OperatingMode::Service);
}
void test_profile_change_persists_but_mode_does_not() {
    TEST_ASSERT_TRUE(fixture->start());
    post("/api/lumasense/profile", "{\"profile\":4}");
    post("/api/lumasense/mode", "{\"mode\":\"OFF\"}");
    TEST_ASSERT_TRUE(fixture->app.begin(0U));
    TEST_ASSERT_EQUAL_UINT8(3U, fixture->app.config().activeProfileIndex);
    assertMode(OperatingMode::Normal);
}

} // namespace

void setUp() {
    Preferences::reset();
    Wire.reset();
    fillRtc();
    fixture = new Fixture();
    TEST_ASSERT_NOT_NULL(fixture);
}
void tearDown() {
    delete fixture;
    fixture = nullptr;
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_status_endpoint_works);
    RUN_TEST(test_status_contains_mode);
    RUN_TEST(test_status_contains_active_profile);
    RUN_TEST(test_status_contains_day_state);
    RUN_TEST(test_status_contains_eight_channels);
    RUN_TEST(test_status_has_no_secrets);
    RUN_TEST(test_valid_profile_command);
    RUN_TEST(test_invalid_profile_rejected);
    RUN_TEST(test_normal_command);
    RUN_TEST(test_off_command);
    RUN_TEST(test_service_command);
    RUN_TEST(test_manual_command);
    RUN_TEST(test_manual_values_go_through_core);
    RUN_TEST(test_manual_below_zero_rejected);
    RUN_TEST(test_manual_above_100_rejected);
    RUN_TEST(test_invalid_manual_timeout_rejected);
    RUN_TEST(test_web_does_not_call_hardware_directly);
    RUN_TEST(test_network_loss_does_not_stop_core);
    RUN_TEST(test_reconnect_does_not_reset_mode);
    RUN_TEST(test_web_failure_does_not_stop_core);
    RUN_TEST(test_restart_does_not_restore_manual_or_off);
    RUN_TEST(test_consecutive_requests_do_not_corrupt_state);
    RUN_TEST(test_malformed_post_returns_400);
    RUN_TEST(test_unknown_endpoint_returns_404);
    RUN_TEST(test_status_json_escapes_profile_name);
    RUN_TEST(test_dashboard_renders_identity);
    RUN_TEST(test_dashboard_uses_aqua_shell);
    RUN_TEST(test_dashboard_uses_1500ms_polling);
    RUN_TEST(test_dashboard_uses_panel_rows_and_both_channel_levels);
    RUN_TEST(test_dashboard_has_exactly_one_row_for_each_channel);
    RUN_TEST(test_navigation_renders_each_lumasense_item_once);
    RUN_TEST(test_control_renders_one_manual_action_set);
    RUN_TEST(test_system_renders_restart_reason_once);
    RUN_TEST(test_control_keeps_all_timeout_choices_and_active_state);
    RUN_TEST(test_diagnostics_and_system_use_structured_cards);
    RUN_TEST(test_polling_does_not_change_state);
    RUN_TEST(test_system_and_diagnostics_apis_still_work);
    RUN_TEST(test_no_password_in_any_page_or_api);
    RUN_TEST(test_exit_manual_returns_to_previous_mode);
    RUN_TEST(test_profile_change_persists_but_mode_does_not);
    UNITY_END();
}
void loop() {}
