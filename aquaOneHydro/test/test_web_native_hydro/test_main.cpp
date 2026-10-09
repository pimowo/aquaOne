#include <unity.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>

#include "web/HydroNativeWeb.h"
#include "web/HydroWebApplication.h"

// ESP32-S3 build/link proof. Runtime remains intentionally skipped.
void setup()
{
    UNITY_BEGIN();
    TEST_ASSERT_EQUAL_UINT(1536U, HydroNativeWeb::SETTINGS_BODY_LIMIT);
    TEST_ASSERT_EQUAL_UINT(32U, HydroNativeWeb::CONTROL_BODY_LIMIT);
    TEST_ASSERT_EQUAL_UINT(1000U, HydroNativeWeb::ACTION_WAIT_TIMEOUT_MS);
    UNITY_END();
}
void loop() {}

#else

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

#include <AquaCore/System/DeviceIdentity.h>
#include <AquaCore/System/SystemService.h>
#include <AquaCore/Web/NativeWebService.h>

#include "web/HydroNativeWeb.h"
#include "web/HydroWebApplication.h"
#include "web/HydroWebProtocol.h"
#include "hydrosense/HydroSenseConfigValidator.h"
#include "hardware/Buzzer.h"
#include "hardware/Pump.h"
#include "hardware/UltrasonicSensor.h"

using namespace AquaCore::Web;

namespace ArduinoTest
{
GpioOperation gpioOperations[8U] {};
size_t gpioOperationCount = 0U;

void resetGpioOperations()
{
    gpioOperationCount = 0U;
}

void recordPinMode(uint8_t pin, uint8_t mode)
{
    if (gpioOperationCount < 8U)
    {
        gpioOperations[gpioOperationCount++] = {
            GpioOperationKind::PinMode, pin, mode
        };
    }
}

void recordDigitalWrite(uint8_t pin, uint8_t value)
{
    if (gpioOperationCount < 8U)
    {
        gpioOperations[gpioOperationCount++] = {
            GpioOperationKind::DigitalWrite, pin, value
        };
    }
}
}

namespace
{
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

void test_gpio_initialization_order_and_safe_levels()
{
    ArduinoTest::resetGpioOperations();
    Pump pump(1U);
    pump.begin();
    TEST_ASSERT_EQUAL_UINT(2U, ArduinoTest::gpioOperationCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::PinMode),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[0U].kind));
    TEST_ASSERT_EQUAL_UINT8(1U, ArduinoTest::gpioOperations[0U].pin);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT, ArduinoTest::gpioOperations[0U].value);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::DigitalWrite),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[1U].kind));
    TEST_ASSERT_EQUAL_UINT8(LOW, ArduinoTest::gpioOperations[1U].value);
    TEST_ASSERT_FALSE(pump.isOn());

    ArduinoTest::resetGpioOperations();
    Buzzer buzzer(12U);
    buzzer.configure(true);
    buzzer.begin();
    TEST_ASSERT_EQUAL_UINT(2U, ArduinoTest::gpioOperationCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::PinMode),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[0U].kind));
    TEST_ASSERT_EQUAL_UINT8(12U, ArduinoTest::gpioOperations[0U].pin);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT, ArduinoTest::gpioOperations[0U].value);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::DigitalWrite),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[1U].kind));
    TEST_ASSERT_EQUAL_UINT8(LOW, ArduinoTest::gpioOperations[1U].value);
    TEST_ASSERT_FALSE(buzzer.isOn());

    ArduinoTest::resetGpioOperations();
    UltrasonicSensor ultrasonic(2U, 3U);
    ultrasonic.begin();
    TEST_ASSERT_EQUAL_UINT(3U, ArduinoTest::gpioOperationCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::PinMode),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[0U].kind));
    TEST_ASSERT_EQUAL_UINT8(2U, ArduinoTest::gpioOperations[0U].pin);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT, ArduinoTest::gpioOperations[0U].value);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::DigitalWrite),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[1U].kind));
    TEST_ASSERT_EQUAL_UINT8(LOW, ArduinoTest::gpioOperations[1U].value);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ArduinoTest::GpioOperationKind::PinMode),
        static_cast<uint8_t>(ArduinoTest::gpioOperations[2U].kind));
    TEST_ASSERT_EQUAL_UINT8(3U, ArduinoTest::gpioOperations[2U].pin);
    TEST_ASSERT_EQUAL_UINT8(INPUT, ArduinoTest::gpioOperations[2U].value);
}

class SnapshotLock final : public SnapshotSynchronizer
{
public:
    bool lock() override { mutex_.lock(); return true; }
    void unlock() override { mutex_.unlock(); }
private:
    std::mutex mutex_;
};

class ActionLock final : public ActionBridgeSynchronizer
{
public:
    bool lock() override { metadata_.lock(); return true; }
    void unlock() override { metadata_.unlock(); }
    size_t completionSlotCapacity() const override { return 4U; }
    bool prepareCompletion(size_t slot) override
    {
        if (slot >= 4U) return false;
        std::lock_guard<std::mutex> guard(signalsMutex_);
        signaled_[slot] = false;
        return true;
    }
    ActionBridgeWaitStatus waitForCompletion(size_t slot, uint32_t timeoutMs) override
    {
        if (slot >= 4U) return ActionBridgeWaitStatus::Failure;
        std::unique_lock<std::mutex> lock(signalsMutex_);
        const bool ready = signals_[slot].wait_for(
            lock, std::chrono::milliseconds(timeoutMs),
            [&]() { return signaled_[slot]; });
        return ready ? ActionBridgeWaitStatus::Signaled
                     : ActionBridgeWaitStatus::TimedOut;
    }
    bool signalCompletion(size_t slot) override
    {
        if (slot >= 4U) return false;
        {
            std::lock_guard<std::mutex> guard(signalsMutex_);
            signaled_[slot] = true;
        }
        signals_[slot].notify_one();
        return true;
    }
private:
    std::mutex metadata_;
    std::mutex signalsMutex_;
    std::condition_variable signals_[4U];
    bool signaled_[4U] {};
};

class Writer final : public WebResponseWriter
{
public:
    bool beginResponse(uint16_t code, ContentType type) override
    { status = code; contentType = type; body.clear(); return true; }
    bool write(const char* data, size_t length) override
    { body.append(data, length); return true; }
    bool endResponse() override { ended = true; return true; }
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    std::string body;
    bool ended = false;
};

class FakeTransport final : public HttpServerTransport
{
public:
    bool addRoute(const char* path, HttpMethod method, HttpRouteHandler handler,
                  void* context, const HttpRouteOptions& options) override
    {
        if (failRegistration) return false;
        return registry.addRoute(path, method, handler, context, options);
    }
    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) override
    { return registry.setNotFoundHandler(handler, context); }
    bool begin(uint16_t) override
    { registry.freeze(); running = !failBegin; return running; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }

    Writer request(const char* path, HttpMethod method,
                   const char* body = nullptr, size_t length = 0U)
    {
        Writer output;
        for (size_t index = 0U; index < registry.size(); ++index)
        {
            const HttpRouteRegistry::Route* route = registry.routeAt(index);
            if (route->method != method || std::strcmp(route->path, path) != 0)
                continue;
            if (length > route->options.maxBodyLength)
            {
                output.beginResponse(413U, ContentType::PlainText);
                output.writeText("Payload Too Large");
                output.endResponse();
                return output;
            }
            if (length != 0U) std::memcpy(storage, body, length);
            storage[length] = '\0';
            const HttpRouteRequest request {
                method, path, length == 0U ? nullptr : storage, length
            };
            route->handler(route->context, request, output);
            return output;
        }
        if (registry.notFoundHandler() != nullptr)
            registry.notFoundHandler()(registry.notFoundContext(), output);
        return output;
    }

    HttpRouteRegistry registry;
    char storage[HTTP_NORMAL_BODY_CAPACITY + 1U] {};
    bool failRegistration = false;
    bool failBegin = false;
    bool running = false;
};

class StorageFake final : public HydroConfigPersistence
{
public:
    bool save(const HydroSenseConfig& value) override
    {
        ++saveCalls;
        if (active != nullptr) activeObservedDuringSave = *active;
        saved = value;
        return saveSucceeds;
    }
    AquaCore::Config::StorageStatus status() const override { return valueStatus; }
    bool saveSucceeds = true;
    size_t saveCalls = 0U;
    const HydroSenseConfig* active = nullptr;
    HydroSenseConfig activeObservedDuringSave {};
    HydroSenseConfig saved {};
    AquaCore::Config::StorageStatus valueStatus {};
};

class TopupFake final : public HydroTopupActions
{
public:
    bool isServiceMode() const override { return serviceMode; }
    void setServiceMode(bool enabled) override
    { serviceMode = enabled; ++setCalls; }
    void resetLockout() override { ++resetCalls; }
    bool serviceMode = false;
    size_t setCalls = 0U;
    size_t resetCalls = 0U;
};

class BuzzerFake final : public HydroBuzzerActions
{
public:
    void mute() override { ++muteCalls; }
    size_t muteCalls = 0U;
};

class SystemBackendFake final : public AquaCore::SystemBackend
{
public:
    uint32_t uptimeMs() const override { return 1234U; }
    AquaCore::RestartReason restartReason() const override
    { return AquaCore::RestartReason::PowerOn; }
};

class NetworkBackendFake final : public AquaCore::Network::NetworkBackend
{
public:
    bool applyRadioPolicy(AquaCore::Network::TriStateSetting,
                          AquaCore::Network::TriStateSetting,
                          AquaCore::Network::WifiPowerSaveMode) override
    { return true; }
    bool setHostname(const char*) override { return true; }
    bool beginSta(const char*, const char*) override { return true; }
    AquaCore::Network::BackendStaState staState() const override { return state; }
    bool reconnectSta() override { return true; }
    bool disconnectSta() override { return true; }
    AquaCore::Network::IpAddress localIp() const override
    {
        AquaCore::Network::IpAddress value {};
        value.octets[0] = 192U;
        value.octets[1] = 168U;
        value.octets[2] = 1U;
        value.octets[3] = 20U;
        return value;
    }
    int32_t rssi() const override { return -51; }
    AquaCore::Network::NetworkDisconnectReason consumeDisconnectReason() override
    { return AquaCore::Network::NetworkDisconnectReason::None; }
    bool startAccessPoint(const char*, const char*) override { return true; }
    bool stopAccessPoint() override { return true; }
    AquaCore::Network::IpAddress accessPointIp() const override
    {
        AquaCore::Network::IpAddress value {};
        value.octets[0] = 192U;
        value.octets[1] = 168U;
        value.octets[2] = 4U;
        value.octets[3] = 1U;
        return value;
    }

    AquaCore::Network::BackendStaState state =
        AquaCore::Network::BackendStaState::Disconnected;
};

const char* validForm(const char* password = "", const char* apPassword = "")
{
    static char body[1024U];
    std::snprintf(body, sizeof(body),
        "floatActiveLow=1&floatUsePullup=1&floatDebounceMs=100&"
        "ultrasonicMinDistanceCm=2&ultrasonicMaxDistanceCm=450&"
        "ultrasonicTimeoutUs=30000&tankEmptyDistanceCm=40&"
        "tankFullDistanceCm=5&tankSampleIntervalMs=80&"
        "tankMaxFailedSeries=3&reserveLowPercent=25&"
        "reserveCriticalPercent=10&reserveHysteresisPercent=3&"
        "topupStartDelayMs=3000&topupMaxPumpRuntimeMs=120000&"
        "staEnabled=1&ssid=Test+Network&password=%s&hostname=hydro-test&"
        "autoReconnect=1&reconnectMs=10000&apEnabled=1&"
        "apSsid=Hydro-Test&apPassword=%s", password, apPassword);
    return body;
}

std::string replaceField(
    const std::string& source, const char* key, const std::string& value)
{
    const std::string prefix = std::string(key) + "=";
    const size_t start = source.find(prefix);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, start);
    const size_t valueStart = start + prefix.size();
    const size_t end = source.find('&', valueStart);
    std::string result = source;
    result.replace(valueStart,
        (end == std::string::npos ? source.size() : end) - valueStart, value);
    return result;
}

std::string removeField(const std::string& source, const char* key)
{
    const std::string prefix = std::string(key) + "=";
    const size_t start = source.find(prefix);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, start);
    const size_t end = source.find('&', start);
    std::string result = source;
    if (end == std::string::npos)
    {
        const size_t eraseStart = start == 0U ? 0U : start - 1U;
        result.erase(eraseStart);
    }
    else
    {
        result.erase(start, end - start + 1U);
    }
    return result;
}

struct Fixture
{
    explicit Fixture(bool failRegistration = false, bool failBegin = false)
        : system(systemBackend), systemSnapshot(snapshotLock), diagnosticsSnapshot(snapshotLock),
          statusSnapshot(snapshotLock), settingsSnapshot(snapshotLock),
          hydroDiagnosticsSnapshot(snapshotLock), bridge(actionLock),
          application(config, storage, topup, buzzer, status, system,
                      statusSnapshot, settingsSnapshot,
                      hydroDiagnosticsSnapshot, bridge),
          service(transport, systemSnapshot, diagnosticsSnapshot),
          web(service, statusSnapshot, settingsSnapshot,
              hydroDiagnosticsSnapshot, bridge)
    {
        std::strcpy(config.wifiPassword, "TEST_WIFI_SECRET_A");
        std::strcpy(config.wifiApPassword, "TEST_AP_SECRET_B");
        storage.active = &config;
        AquaCore::DeviceIdentity identity(
            "hydro-test", "Hydro Test", "test", "native");
        system.begin(identity);
        CoreSystemProjection systemValue {};
        systemValue.ready = true;
        systemValue.identity = identity;
        const uint8_t bytes[] {0x24U, 0x6FU, 0x28U, 0xA1U, 0xB2U, 0xC3U};
        AquaCore::Identity::DeviceId id;
        TEST_ASSERT_TRUE(id.assign(bytes, sizeof(bytes)).isValid());
        TEST_ASSERT_TRUE(systemValue.canonicalIdentity.assign("hydro", id).isValid());
        systemSnapshot.publish(systemValue);
        CoreDiagnosticsProjection diagnosticsValue {};
        diagnosticsSnapshot.publish(diagnosticsValue);
        application.publish();
        TEST_ASSERT_TRUE(web.registerRoutes());
        transport.failRegistration = failRegistration;
        transport.failBegin = failBegin;
        WebConfig webConfig {};
        webConfig.enabled = true;
        const bool started = service.begin(webConfig);
        TEST_ASSERT_EQUAL(!failRegistration && !failBegin, started);
    }

    HydroWebResult submitAndProcess(const HydroWebRequest& request)
    {
        ApplicationBridgeToken token {};
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ApplicationBridgeSubmitResult::Accepted),
            static_cast<uint8_t>(bridge.submit(request, token)));
        TEST_ASSERT_TRUE(application.processOne());
        HydroWebResult result = HydroWebResult::StorageFailure;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ApplicationBridgeWaitResult::Completed),
            static_cast<uint8_t>(bridge.wait(token, 0U, result)));
        return result;
    }

    SnapshotLock snapshotLock;
    ActionLock actionLock;
    PublishedSnapshot<CoreSystemProjection> systemSnapshot;
    PublishedSnapshot<CoreDiagnosticsProjection> diagnosticsSnapshot;
    PublishedSnapshot<SystemStatus> statusSnapshot;
    PublishedSnapshot<HydroSettingsProjection> settingsSnapshot;
    PublishedSnapshot<HydroDiagnosticsProjection> hydroDiagnosticsSnapshot;
    HydroApplicationBridge bridge;
    HydroSenseConfig config {};
    StorageFake storage;
    TopupFake topup;
    BuzzerFake buzzer;
    SystemStatus status {};
    SystemBackendFake systemBackend;
    AquaCore::SystemService system;
    HydroWebApplication application;
    FakeTransport transport;
    NativeWebService service;
    HydroNativeWeb web;
};

void test_routes_pages_theme_secrets_and_snapshot_isolation()
{
    Fixture fixture;
    TEST_ASSERT_EQUAL_UINT(10U, fixture.transport.registry.size());
    size_t gets = 0U, posts = 0U;
    for (size_t index = 0U; index < fixture.transport.registry.size(); ++index)
    {
        const HttpRouteRegistry::Route* route = fixture.transport.registry.routeAt(index);
        if (route->method == HttpMethod::Get) ++gets; else ++posts;
    }
    TEST_ASSERT_EQUAL_UINT(8U, gets);
    TEST_ASSERT_EQUAL_UINT(2U, posts);
    const char* pages[] = {"/", "/control", "/settings", "/diagnostics"};
    for (size_t index = 0U; index < 4U; ++index)
    {
        const Writer page = fixture.transport.request(pages[index], HttpMethod::Get);
        TEST_ASSERT_EQUAL_UINT16(200U, page.status);
        TEST_ASSERT_NOT_EQUAL(std::string::npos, page.body.find("aqua.css"));
    }
    const Writer css = fixture.transport.request("/assets/aqua.css", HttpMethod::Get);
    TEST_ASSERT_EQUAL_UINT16(200U, css.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        css.body.find(":root{color-scheme:dark;--bg"));
    const Writer settings = fixture.transport.request("/settings", HttpMethod::Get);
    TEST_ASSERT_EQUAL(std::string::npos, settings.body.find("TEST_WIFI_SECRET_A"));
    TEST_ASSERT_EQUAL(std::string::npos, settings.body.find("TEST_AP_SECRET_B"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        settings.body.find("name=\"password\" maxlength=\"64\" value=\"\""));

    const Writer systemApi = fixture.transport.request("/api/system", HttpMethod::Get);
    TEST_ASSERT_EQUAL_UINT16(200U, systemApi.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"deviceType\":\"hydro-test\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"device_type\":\"hydro\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, systemApi.body.find("\"device_id\":\"246F28A1B2C3\""));
    TEST_ASSERT_TRUE(hasApiProtocolVersion(systemApi.body.c_str(), 1, 0));

    Writer api = fixture.transport.request("/api/hydrosense", HttpMethod::Get);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, api.body.find("\"serviceMode\":false"));
    fixture.status.serviceMode = true;
    api = fixture.transport.request("/api/hydrosense", HttpMethod::Get);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, api.body.find("\"serviceMode\":false"));
    fixture.application.publish();
    api = fixture.transport.request("/api/hydrosense", HttpMethod::Get);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, api.body.find("\"serviceMode\":true"));
    TEST_ASSERT_EQUAL(std::string::npos, api.body.find("password"));
    TEST_ASSERT_EQUAL_UINT16(404U,
        fixture.transport.request("/missing", HttpMethod::Get).status);
}

void test_parser_boundaries_and_secret_semantics()
{
    HydroSettingsRequest settings {};
    const char* body = validForm();
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(body, std::strlen(body), settings));
    TEST_ASSERT_EQUAL_STRING("Test Network", settings.wifiSsid);
    TEST_ASSERT_FALSE(settings.replaceWifiPassword);
    TEST_ASSERT_FALSE(settings.replaceApPassword);
    TEST_ASSERT_FALSE(parseHydroSettingsRequest("ssid=bad%Q0", 11U, settings));
    const char embedded[] = {'a', '=', '1', '\0', '&', 'b', '=', '2'};
    TEST_ASSERT_FALSE(parseHydroSettingsRequest(embedded, sizeof(embedded), settings));
    HydroWebRequest action {};
    TEST_ASSERT_TRUE(parseHydroControlRequest(
        "action=service_toggle", 21U, action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebRequestKind::ToggleServiceMode),
                            static_cast<uint8_t>(action.kind));
    TEST_ASSERT_FALSE(parseHydroControlRequest("action=bad", 10U, action));
    TEST_ASSERT_FALSE(parseHydroControlRequest(
        "action=mute&action=mute", 23U, action));
    TEST_ASSERT_FALSE(parseHydroControlRequest(
        "action=mute&extra=1", 19U, action));

    body = validForm("NEW_WIFI_SECRET", "NEW_AP_SECRET");
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(body, std::strlen(body), settings));
    TEST_ASSERT_TRUE(settings.replaceWifiPassword);
    TEST_ASSERT_TRUE(settings.replaceApPassword);
}

void test_parser_strict_field_set_and_decoded_capacities()
{
    HydroSettingsRequest parsed {};
    const std::string base = validForm();

    TEST_ASSERT_FALSE(parseHydroSettingsRequest(
        (base + "&ssid=duplicate").data(), base.size() + 15U, parsed));
    TEST_ASSERT_FALSE(parseHydroSettingsRequest(
        (base + "&unknown=value").data(), base.size() + 14U, parsed));
    const std::string missing = removeField(base, "tankSampleIntervalMs");
    TEST_ASSERT_FALSE(parseHydroSettingsRequest(
        missing.data(), missing.size(), parsed));
    const std::string incompletePercent = replaceField(base, "ssid", "bad%");
    TEST_ASSERT_FALSE(parseHydroSettingsRequest(
        incompletePercent.data(), incompletePercent.size(), parsed));
    const std::string nulPercent = replaceField(base, "ssid", "%00");
    TEST_ASSERT_FALSE(parseHydroSettingsRequest(
        nulPercent.data(), nulPercent.size(), parsed));

    const char* fields[] = {"ssid", "hostname", "apSsid"};
    for (size_t index = 0U; index < 3U; ++index)
    {
        const std::string exact = replaceField(base, fields[index],
                                               std::string(32U, 's'));
        TEST_ASSERT_TRUE(parseHydroSettingsRequest(
            exact.data(), exact.size(), parsed));
        const std::string tooLong = replaceField(base, fields[index],
                                                 std::string(33U, 's'));
        TEST_ASSERT_FALSE(parseHydroSettingsRequest(
            tooLong.data(), tooLong.size(), parsed));
    }
    const char* secretFields[] = {"password", "apPassword"};
    for (size_t index = 0U; index < 2U; ++index)
    {
        const std::string exact = replaceField(base, secretFields[index],
                                               std::string(64U, 'p'));
        TEST_ASSERT_TRUE(parseHydroSettingsRequest(
            exact.data(), exact.size(), parsed));
        const std::string tooLong = replaceField(base, secretFields[index],
                                                 std::string(65U, 'p'));
        TEST_ASSERT_FALSE(parseHydroSettingsRequest(
            tooLong.data(), tooLong.size(), parsed));
    }

    std::string withoutSecrets = removeField(base, "password");
    withoutSecrets = removeField(withoutSecrets, "apPassword");
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(
        withoutSecrets.data(), withoutSecrets.size(), parsed));
    TEST_ASSERT_FALSE(parsed.replaceWifiPassword);
    TEST_ASSERT_FALSE(parsed.replaceApPassword);

    HydroSenseConfig nanConfig {};
    nanConfig.reserveLowPercent = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(validateHydroSenseConfig(nanConfig));
}

void test_application_settings_results_and_control_effects()
{
    Fixture fixture;
    HydroWebRequest request {};
    request.kind = HydroWebRequestKind::SaveSettings;
    const char* body = validForm();
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(body, std::strlen(body), request.settings));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_STRING("TEST_WIFI_SECRET_A", fixture.config.wifiPassword);
    TEST_ASSERT_EQUAL_STRING("TEST_AP_SECRET_B", fixture.config.wifiApPassword);
    TEST_ASSERT_TRUE(fixture.application.restartRequested());
    TEST_ASSERT_FALSE(fixture.storage.activeObservedDuringSave.wifiStaEnabled);
    TEST_ASSERT_TRUE(fixture.storage.saved.wifiStaEnabled);

    fixture.application.clearRestartRequest();

    std::string missingSecrets = validForm();
    missingSecrets = removeField(missingSecrets, "password");
    missingSecrets = removeField(missingSecrets, "apPassword");
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(
        missingSecrets.data(), missingSecrets.size(), request.settings));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_STRING("TEST_WIFI_SECRET_A", fixture.config.wifiPassword);
    TEST_ASSERT_EQUAL_STRING("TEST_AP_SECRET_B", fixture.config.wifiApPassword);
    fixture.application.clearRestartRequest();
    body = validForm("REPLACED_WIFI", "REPLACED_AP");
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(body, std::strlen(body), request.settings));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_STRING("REPLACED_WIFI", fixture.config.wifiPassword);
    TEST_ASSERT_EQUAL_STRING("REPLACED_AP", fixture.config.wifiApPassword);
    fixture.application.publish();
    const Writer afterReplacement = fixture.transport.request(
        "/settings", HttpMethod::Get);
    TEST_ASSERT_EQUAL(std::string::npos,
        afterReplacement.body.find("REPLACED_WIFI"));
    TEST_ASSERT_EQUAL(std::string::npos,
        afterReplacement.body.find("REPLACED_AP"));
    fixture.application.clearRestartRequest();

    std::string disabled = validForm();
    disabled.erase(disabled.find("staEnabled=1&"), std::strlen("staEnabled=1&"));
    disabled.erase(disabled.find("apEnabled=1&"), std::strlen("apEnabled=1&"));
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(
        disabled.data(), disabled.size(), request.settings));
    TEST_ASSERT_FALSE(request.settings.wifiStaEnabled);
    TEST_ASSERT_FALSE(request.settings.wifiApEnabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_STRING("REPLACED_WIFI", fixture.config.wifiPassword);
    TEST_ASSERT_EQUAL_STRING("REPLACED_AP", fixture.config.wifiApPassword);
    fixture.application.clearRestartRequest();

    request.settings.reserveCriticalPercent = 50.0f;
    request.settings.reserveLowPercent = 25.0f;
    const HydroSenseConfig beforeInvalid = fixture.config;
    const size_t savesBeforeInvalid = fixture.storage.saveCalls;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::ValidationFailure),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_UINT(savesBeforeInvalid, fixture.storage.saveCalls);
    TEST_ASSERT_EQUAL_MEMORY(&beforeInvalid, &fixture.config, sizeof(fixture.config));
    TEST_ASSERT_FALSE(fixture.application.restartRequested());

    body = validForm();
    TEST_ASSERT_TRUE(parseHydroSettingsRequest(body, std::strlen(body), request.settings));
    fixture.storage.saveSucceeds = false;
    const HydroSenseConfig beforeFailure = fixture.config;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::StorageFailure),
                            static_cast<uint8_t>(fixture.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_MEMORY(&beforeFailure, &fixture.config, sizeof(fixture.config));
    TEST_ASSERT_FALSE(fixture.application.restartRequested());

    HydroWebRequest action {};
    action.kind = HydroWebRequestKind::ToggleServiceMode;
    ApplicationBridgeToken token {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplicationBridgeSubmitResult::Accepted),
                            static_cast<uint8_t>(fixture.bridge.submit(action, token)));
    TEST_ASSERT_FALSE(fixture.topup.serviceMode);
    TEST_ASSERT_TRUE(fixture.application.processOne());
    TEST_ASSERT_TRUE(fixture.topup.serviceMode);
    HydroWebResult ignored {};
    fixture.bridge.wait(token, 0U, ignored);
    action.kind = HydroWebRequestKind::MuteBuzzer;
    fixture.submitAndProcess(action);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.buzzer.muteCalls);
    action.kind = HydroWebRequestKind::ResetLockout;
    fixture.submitAndProcess(action);
    TEST_ASSERT_EQUAL_UINT(1U, fixture.topup.resetCalls);
}

void test_http_limits_queue_timeout_and_completed_mapping()
{
    Fixture fixture;
    std::string tooLarge(1537U, 'x');
    TEST_ASSERT_EQUAL_UINT16(413U, fixture.transport.request(
        "/api/settings", HttpMethod::Post, tooLarge.data(), tooLarge.size()).status);
    std::string exact(1536U, 'x');
    TEST_ASSERT_EQUAL_UINT16(400U, fixture.transport.request(
        "/api/settings", HttpMethod::Post, exact.data(), exact.size()).status);
    std::string controlLarge(33U, 'x');
    TEST_ASSERT_EQUAL_UINT16(413U, fixture.transport.request(
        "/api/control", HttpMethod::Post,
        controlLarge.data(), controlLarge.size()).status);

    ApplicationBridgeToken occupied[4U] {};
    HydroWebRequest queued {};
    for (size_t index = 0U; index < 4U; ++index)
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplicationBridgeSubmitResult::Accepted),
            static_cast<uint8_t>(fixture.bridge.submit(queued, occupied[index])));
    TEST_ASSERT_EQUAL_UINT16(503U, fixture.transport.request(
        "/api/control", HttpMethod::Post, "action=mute", 11U).status);

    Fixture timeoutFixture;
    const Writer timeout = timeoutFixture.transport.request(
        "/api/control", HttpMethod::Post, "action=mute", 11U);
    TEST_ASSERT_EQUAL_UINT16(202U, timeout.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, timeout.body.find("OUTCOME UNKNOWN"));
    TEST_ASSERT_EQUAL_UINT(0U, timeoutFixture.buzzer.muteCalls);
    TEST_ASSERT_TRUE(timeoutFixture.application.processOne());
    TEST_ASSERT_EQUAL_UINT(1U, timeoutFixture.buzzer.muteCalls);

    Fixture completedFixture;
    Writer completed;
    std::thread http([&]() {
        completed = completedFixture.transport.request(
            "/api/control", HttpMethod::Post, "action=mute", 11U);
    });
    while (completedFixture.bridge.pendingCount() == 0U)
        std::this_thread::yield();
    TEST_ASSERT_TRUE(completedFixture.application.processOne());
    http.join();
    TEST_ASSERT_EQUAL_UINT16(200U, completed.status);
    TEST_ASSERT_EQUAL_STRING("OK", completed.body.c_str());
}

void test_settings_http_result_mapping_and_core_builtins()
{
    Fixture success;
    const char* body = validForm();
    Writer response;
    std::thread http([&]() {
        response = success.transport.request(
            "/api/settings", HttpMethod::Post, body, std::strlen(body));
    });
    while (success.bridge.pendingCount() == 0U) std::this_thread::yield();
    TEST_ASSERT_TRUE(success.application.processOne());
    http.join();
    TEST_ASSERT_EQUAL_UINT16(200U, response.status);
    TEST_ASSERT_TRUE(success.application.restartRequested());

    Fixture invalid;
    std::string invalidBody = validForm();
    const size_t critical = invalidBody.find("reserveCriticalPercent=10");
    invalidBody.replace(critical, std::strlen("reserveCriticalPercent=10"),
                        "reserveCriticalPercent=50");
    std::thread invalidHttp([&]() {
        response = invalid.transport.request("/api/settings", HttpMethod::Post,
                                             invalidBody.data(), invalidBody.size());
    });
    while (invalid.bridge.pendingCount() == 0U) std::this_thread::yield();
    TEST_ASSERT_TRUE(invalid.application.processOne());
    invalidHttp.join();
    TEST_ASSERT_EQUAL_UINT16(400U, response.status);
    TEST_ASSERT_EQUAL_UINT(0U, invalid.storage.saveCalls);
    TEST_ASSERT_FALSE(invalid.application.restartRequested());

    Fixture failed;
    failed.storage.saveSucceeds = false;
    std::thread failedHttp([&]() {
        response = failed.transport.request(
            "/api/settings", HttpMethod::Post, body, std::strlen(body));
    });
    while (failed.bridge.pendingCount() == 0U) std::this_thread::yield();
    TEST_ASSERT_TRUE(failed.application.processOne());
    failedHttp.join();
    TEST_ASSERT_EQUAL_UINT16(500U, response.status);
    TEST_ASSERT_FALSE(failed.application.restartRequested());

    TEST_ASSERT_EQUAL_UINT16(400U, failed.transport.request(
        "/api/settings", HttpMethod::Post, "bad=%XX", 7U).status);
    TEST_ASSERT_FALSE(failed.application.restartRequested());
    TEST_ASSERT_EQUAL_UINT16(200U,
        failed.transport.request("/api/system", HttpMethod::Get).status);
    TEST_ASSERT_EQUAL_UINT16(200U,
        failed.transport.request("/api/diagnostics", HttpMethod::Get).status);
}

void test_core_diagnostics_source_uses_live_clock_and_redacts_identity()
{
    SystemBackendFake systemBackend;
    AquaCore::SystemService system(systemBackend);
    const AquaCore::DeviceIdentity identity(
        "hydro-test", "Hydro Test", "test", "native");
    TEST_ASSERT_TRUE(system.begin(identity));

    NetworkBackendFake networkBackend;
    AquaCore::Network::NetworkService network(networkBackend);
    AquaCore::Network::NetworkConfig config {};
    config.staEnabled = true;
    std::strcpy(config.ssid, "PRIVATE_SSID");
    std::strcpy(config.password, "PRIVATE_PASSWORD");
    std::strcpy(config.hostname, "PRIVATE_HOSTNAME");
    TEST_ASSERT_TRUE(network.begin(config));
    networkBackend.state = AquaCore::Network::BackendStaState::Connected;
    network.update(1000U);
    network.update(1250U);

    StorageFake storage;
    storage.valueStatus.backendReady = true;
    storage.valueStatus.hasValidPayload = true;
    storage.valueStatus.lastLoadResult =
        AquaCore::Config::StorageOperationResult::Success;
    storage.valueStatus.lastSaveResult =
        AquaCore::Config::StorageOperationResult::VerifyFailure;
    HydroCoreDiagnosticsSource source(system, network, storage);
    CoreDiagnosticsProjection projection {};
    TEST_ASSERT_TRUE(source.read(projection));
    TEST_ASSERT_EQUAL_UINT32(250U,
        projection.value.network.connectionUptimeMs);
    TEST_ASSERT_EQUAL_STRING("", projection.value.network.ssid);
    TEST_ASSERT_EQUAL_STRING("", projection.value.network.hostname);
    TEST_ASSERT_EQUAL_STRING("", projection.value.network.apSsid);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(AquaCore::Diagnostics::HealthState::Unknown),
        static_cast<uint8_t>(projection.value.timeHealth));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(AquaCore::Config::StorageOperationResult::VerifyFailure),
        static_cast<uint8_t>(projection.value.storage.lastSaveResult));
}

void test_web_failure_does_not_block_application_processing()
{
    Fixture registrationFailure(true, false);
    TEST_ASSERT_FALSE(registrationFailure.service.isRunning());
    HydroWebRequest request {};
    request.kind = HydroWebRequestKind::MuteBuzzer;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
        static_cast<uint8_t>(registrationFailure.submitAndProcess(request)));
    TEST_ASSERT_EQUAL_UINT(1U, registrationFailure.buzzer.muteCalls);

    Fixture beginFailure(false, true);
    TEST_ASSERT_FALSE(beginFailure.service.isRunning());
    request.kind = HydroWebRequestKind::ToggleServiceMode;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HydroWebResult::CompletedSuccess),
        static_cast<uint8_t>(beginFailure.submitAndProcess(request)));
    TEST_ASSERT_TRUE(beginFailure.topup.serviceMode);
}
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_gpio_initialization_order_and_safe_levels);
    RUN_TEST(test_routes_pages_theme_secrets_and_snapshot_isolation);
    RUN_TEST(test_parser_boundaries_and_secret_semantics);
    RUN_TEST(test_parser_strict_field_set_and_decoded_capacities);
    RUN_TEST(test_application_settings_results_and_control_effects);
    RUN_TEST(test_http_limits_queue_timeout_and_completed_mapping);
    RUN_TEST(test_settings_http_result_mapping_and_core_builtins);
    RUN_TEST(test_core_diagnostics_source_uses_live_clock_and_redacts_identity);
    RUN_TEST(test_web_failure_does_not_block_application_processing);
    return UNITY_END();
}

#endif
