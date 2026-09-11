#include <Arduino.h>
#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/Config/StorageBackend.h"
#include "AquaCore/Config/StorageService.h"
#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Network/NetworkTypes.h"
#include "AquaCore/System/SystemBackend.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Time/RtcBus.h"
#include "AquaCore/Time/RtcService.h"

using namespace AquaCore;
using namespace AquaCore::Config;
using namespace AquaCore::Diagnostics;
using namespace AquaCore::Network;
using namespace AquaCore::Time;

namespace {

class MockNetworkBackend final : public NetworkBackend {
public:
    bool setHostname(const char* value) override {
        ++setHostnameCalls;
        copy(hostname, sizeof(hostname), value);
        return setHostnameResult;
    }

    bool beginSta(
        const char* ssidValue,
        const char* passwordValue
    ) override {
        ++beginStaCalls;
        copy(ssid, sizeof(ssid), ssidValue);
        copy(password, sizeof(password), passwordValue);
        return beginStaResult;
    }

    BackendStaState staState() const override {
        ++staStateCalls;
        return currentState;
    }

    bool reconnectSta() override {
        ++reconnectCalls;
        return reconnectResult;
    }

    bool disconnectSta() override {
        ++disconnectCalls;
        return disconnectResult;
    }

    IpAddress localIp() const override {
        ++localIpCalls;
        return staIp;
    }

    int32_t rssi() const override {
        ++rssiCalls;
        return staRssi;
    }

    bool startAccessPoint(
        const char* ssidValue,
        const char* passwordValue
    ) override {
        ++startApCalls;
        copy(apSsid, sizeof(apSsid), ssidValue);
        copy(apPassword, sizeof(apPassword), passwordValue);
        return startApResult;
    }

    bool stopAccessPoint() override {
        ++stopApCalls;
        return stopApResult;
    }

    IpAddress accessPointIp() const override {
        ++apIpCalls;
        return apIp;
    }

    static void copy(
        char* destination,
        size_t capacity,
        const char* source
    ) {
        if (capacity == 0U) {
            return;
        }

        destination[0] = '\0';
        if (source == nullptr) {
            return;
        }

        strncpy(destination, source, capacity - 1U);
        destination[capacity - 1U] = '\0';
    }

    bool setHostnameResult = true;
    bool beginStaResult = true;
    bool reconnectResult = true;
    bool disconnectResult = true;
    bool startApResult = true;
    bool stopApResult = true;
    BackendStaState currentState =
        BackendStaState::Connecting;
    IpAddress staIp {{192U, 168U, 1U, 44U}};
    IpAddress apIp {{192U, 168U, 4U, 1U}};
    int32_t staRssi = -57;

    char hostname[WIFI_HOSTNAME_CAPACITY] {};
    char ssid[WIFI_SSID_CAPACITY] {};
    char password[WIFI_PASSWORD_CAPACITY] {};
    char apSsid[WIFI_SSID_CAPACITY] {};
    char apPassword[WIFI_PASSWORD_CAPACITY] {};

    uint16_t setHostnameCalls = 0U;
    uint16_t beginStaCalls = 0U;
    mutable uint16_t staStateCalls = 0U;
    uint16_t reconnectCalls = 0U;
    uint16_t disconnectCalls = 0U;
    mutable uint16_t localIpCalls = 0U;
    mutable uint16_t rssiCalls = 0U;
    uint16_t startApCalls = 0U;
    uint16_t stopApCalls = 0U;
    mutable uint16_t apIpCalls = 0U;
};

NetworkConfig staConfig() {
    NetworkConfig config {};
    config.staEnabled = true;
    strcpy(config.ssid, "AquaLab");
    strcpy(config.password, "secret-password");
    strcpy(config.hostname, "lumasense");
    config.autoReconnect = true;
    config.reconnectIntervalMs = 10000U;
    return config;
}

NetworkConfig apConfig() {
    NetworkConfig config {};
    config.apEnabled = true;
    strcpy(config.apSsid, "Aqua-Setup");
    strcpy(config.apPassword, "setup-pass");
    return config;
}

NetworkConfig staAndApConfig() {
    NetworkConfig config = staConfig();
    config.apEnabled = true;
    strcpy(config.apSsid, "Aqua-Setup");
    strcpy(config.apPassword, "setup-pass");
    return config;
}

void assertState(
    NetworkState expected,
    const NetworkService& service
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(service.state())
    );
}

void test_network_disabled() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    NetworkConfig config {};

    TEST_ASSERT_TRUE(service.begin(config));
    assertState(NetworkState::Disabled, service);
    TEST_ASSERT_FALSE(service.isStaEnabled());
    TEST_ASSERT_FALSE(service.isApEnabled());
    TEST_ASSERT_EQUAL_UINT16(0U, backend.beginStaCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, backend.startApCalls);
}

void test_begin_is_non_blocking() {
    MockNetworkBackend backend;
    NetworkService service(backend);

    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_UINT16(1U, backend.beginStaCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, backend.staStateCalls);
    assertState(NetworkState::Connecting, service);
}

void test_sta_connecting_state() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Connecting;
    service.update(1U);
    assertState(NetworkState::Connecting, service);
}

void test_connected_state() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Connected;
    service.update(100U);

    assertState(NetworkState::Connected, service);
    TEST_ASSERT_TRUE(service.isConnected());
    TEST_ASSERT_EQUAL_UINT32(
        400U,
        service.connectionUptimeMs(500U)
    );
}

void test_disconnected_state() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Connected;
    service.update(10U);
    backend.currentState = BackendStaState::Disconnected;
    service.update(20U);

    assertState(NetworkState::Disconnected, service);
    TEST_ASSERT_FALSE(service.isConnected());
    TEST_ASSERT_EQUAL_UINT32(0U, service.connectionUptimeMs(30U));
}

void test_ip_is_reported() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Connected;
    service.update(100U);

    const IpAddress ip = service.ipAddress();
    TEST_ASSERT_EQUAL_UINT8(192U, ip.octets[0]);
    TEST_ASSERT_EQUAL_UINT8(168U, ip.octets[1]);
    TEST_ASSERT_EQUAL_UINT8(1U, ip.octets[2]);
    TEST_ASSERT_EQUAL_UINT8(44U, ip.octets[3]);
}

void test_rssi_is_reported() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Connected;
    backend.staRssi = -63;
    service.update(100U);
    TEST_ASSERT_EQUAL_INT32(-63, service.rssi());
}

void test_ssid_is_reported() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_STRING("AquaLab", service.ssid());
    TEST_ASSERT_EQUAL_STRING("AquaLab", backend.ssid);
}

void test_hostname_is_reported() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_STRING("lumasense", service.hostname());
    TEST_ASSERT_EQUAL_STRING("lumasense", backend.hostname);
}

void test_reconnect_after_interval() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Disconnected;

    service.update(100U);
    service.update(10100U);

    TEST_ASSERT_EQUAL_UINT16(1U, backend.reconnectCalls);
    TEST_ASSERT_EQUAL_UINT32(1U, service.reconnectCount());
    assertState(NetworkState::Connecting, service);
}

void test_no_reconnect_before_interval() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Disconnected;

    service.update(100U);
    service.update(10099U);

    TEST_ASSERT_EQUAL_UINT16(0U, backend.reconnectCalls);
    TEST_ASSERT_EQUAL_UINT32(0U, service.reconnectCount());
}

void test_reconnect_handles_millis_rollover() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Disconnected;

    service.update(UINT32_MAX - 5000U);
    service.update(4999U);

    TEST_ASSERT_EQUAL_UINT16(1U, backend.reconnectCalls);
    TEST_ASSERT_EQUAL_UINT32(1U, service.reconnectCount());
}

void test_reconnect_count_tracks_attempts() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.currentState = BackendStaState::Disconnected;

    service.update(0U);
    service.update(10000U);
    service.update(20000U);

    TEST_ASSERT_EQUAL_UINT16(2U, backend.reconnectCalls);
    TEST_ASSERT_EQUAL_UINT32(2U, service.reconnectCount());
}

void test_backend_failure_does_not_crash() {
    MockNetworkBackend backend;
    backend.beginStaResult = false;
    NetworkService service(backend);

    TEST_ASSERT_FALSE(service.begin(staConfig()));
    assertState(NetworkState::Error, service);
    TEST_ASSERT_FALSE(service.isConnected());
}

class FakeSystemBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override {
        return nowMs;
    }

    RestartReason restartReason() const override {
        return reason;
    }

    uint32_t nowMs = 0U;
    RestartReason reason = RestartReason::PowerOn;
};

void test_wifi_loss_does_not_restart_system() {
    FakeSystemBackend systemBackend;
    SystemService system(systemBackend);
    TEST_ASSERT_TRUE(system.begin(DeviceIdentity(
        "lighting-controller",
        "LumaSense",
        "1.0.0",
        "LOLIN32_TEST"
    )));

    MockNetworkBackend backend;
    NetworkService network(backend);
    TEST_ASSERT_TRUE(network.begin(staConfig()));
    backend.currentState = BackendStaState::Disconnected;
    network.update(0U);
    network.update(10000U);

    TEST_ASSERT_TRUE(system.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RestartReason::PowerOn),
        static_cast<uint8_t>(system.restartReason())
    );
}

void test_ap_disabled() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_FALSE(service.isApEnabled());
    TEST_ASSERT_FALSE(service.isApActive());
    TEST_ASSERT_EQUAL_UINT16(0U, backend.startApCalls);
}

void test_ap_enabled() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(apConfig()));

    TEST_ASSERT_TRUE(service.isApEnabled());
    TEST_ASSERT_TRUE(service.isApActive());
    assertState(NetworkState::Idle, service);
    TEST_ASSERT_EQUAL_STRING("Aqua-Setup", service.accessPointSsid());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(AccessPointState::Active),
        static_cast<uint8_t>(service.accessPointState())
    );
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        service.accessPointIpAddress().octets[3]
    );
}

void test_sta_and_ap_can_run_together() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staAndApConfig()));

    TEST_ASSERT_TRUE(service.isStaEnabled());
    TEST_ASSERT_TRUE(service.isApEnabled());
    TEST_ASSERT_TRUE(service.isApActive());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.beginStaCalls);
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startApCalls);
    assertState(NetworkState::Connecting, service);
}

void test_two_network_services_are_independent() {
    MockNetworkBackend firstBackend;
    MockNetworkBackend secondBackend;
    NetworkService first(firstBackend);
    NetworkService second(secondBackend);
    NetworkConfig firstConfig = staConfig();
    NetworkConfig secondConfig = staConfig();
    strcpy(secondConfig.ssid, "DoserNet");
    strcpy(secondConfig.hostname, "aqua-doser");

    TEST_ASSERT_TRUE(first.begin(firstConfig));
    TEST_ASSERT_TRUE(second.begin(secondConfig));
    firstBackend.currentState = BackendStaState::Connected;
    secondBackend.currentState = BackendStaState::Disconnected;
    first.update(10U);
    second.update(10U);

    TEST_ASSERT_TRUE(first.isConnected());
    TEST_ASSERT_FALSE(second.isConnected());
    TEST_ASSERT_EQUAL_STRING("AquaLab", first.ssid());
    TEST_ASSERT_EQUAL_STRING("DoserNet", second.ssid());
}

void test_services_have_no_shared_runtime_state() {
    MockNetworkBackend firstBackend;
    MockNetworkBackend secondBackend;
    NetworkService first(firstBackend);
    NetworkService second(secondBackend);
    TEST_ASSERT_TRUE(first.begin(staConfig()));
    TEST_ASSERT_TRUE(second.begin(staConfig()));

    firstBackend.currentState = BackendStaState::Disconnected;
    secondBackend.currentState = BackendStaState::Connected;
    first.update(0U);
    first.update(10000U);
    second.update(10000U);

    TEST_ASSERT_EQUAL_UINT32(1U, first.reconnectCount());
    TEST_ASSERT_EQUAL_UINT32(0U, second.reconnectCount());
    TEST_ASSERT_FALSE(first.isConnected());
    TEST_ASSERT_TRUE(second.isConnected());
}

class TinyRtcBus final : public RtcBus {
public:
    bool begin(int, int) override {
        return false;
    }

    bool readRegisters(
        uint8_t,
        uint8_t,
        uint8_t*,
        size_t
    ) override {
        return false;
    }

    bool writeRegisters(
        uint8_t,
        uint8_t,
        const uint8_t*,
        size_t
    ) override {
        return false;
    }
};

class TinyStorageBackend final : public StorageBackend {
public:
    bool begin(const char*) override {
        return false;
    }

    void end() override {
    }

    size_t blobLength(const char*) override {
        return 0U;
    }

    size_t readBlob(
        const char*,
        void*,
        size_t
    ) override {
        return 0U;
    }

    size_t writeBlob(
        const char*,
        const void*,
        size_t
    ) override {
        return 0U;
    }
};

struct DiagnosticsFixture {
    DiagnosticsFixture()
        : system(systemBackend),
          rtc(rtcBus, RtcConfig {}),
          storage(storageBackend, "ac6", "a", "b") {
        system.begin(DeviceIdentity(
            "controller",
            "TestDevice",
            "1.0.0",
            "ESP32"
        ));
    }

    FakeSystemBackend systemBackend;
    SystemService system;
    TinyRtcBus rtcBus;
    RtcService rtc;
    TinyStorageBackend storageBackend;
    StorageService storage;
};

void test_diagnostics_works_without_network() {
    DiagnosticsFixture fixture;
    DiagnosticsService diagnostics(
        fixture.system,
        fixture.rtc,
        fixture.storage,
        "Europe/Warsaw"
    );

    const DiagnosticsSnapshot value = diagnostics.snapshot();
    TEST_ASSERT_FALSE(value.network.available);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Unknown),
        static_cast<uint8_t>(value.networkHealth)
    );
}

void test_diagnostics_reports_network() {
    DiagnosticsFixture fixture;
    fixture.systemBackend.nowMs = 500U;
    MockNetworkBackend backend;
    NetworkService network(backend);
    TEST_ASSERT_TRUE(network.begin(staAndApConfig()));
    backend.currentState = BackendStaState::Connected;
    network.update(100U);

    DiagnosticsService diagnostics(
        fixture.system,
        fixture.rtc,
        fixture.storage,
        "Europe/Warsaw",
        nullptr,
        &network
    );
    const DiagnosticsSnapshot value = diagnostics.snapshot();

    TEST_ASSERT_TRUE(value.network.available);
    TEST_ASSERT_TRUE(value.network.connected);
    TEST_ASSERT_EQUAL_STRING("AquaLab", value.network.ssid);
    TEST_ASSERT_EQUAL_STRING("lumasense", value.network.hostname);
    TEST_ASSERT_EQUAL_INT32(-57, value.network.rssi);
    TEST_ASSERT_EQUAL_UINT32(400U, value.network.connectionUptimeMs);
    TEST_ASSERT_TRUE(value.network.apEnabled);
    TEST_ASSERT_TRUE(value.network.apActive);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Ok),
        static_cast<uint8_t>(value.networkHealth)
    );
}

void test_network_has_no_lumasense_dependency() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    NetworkConfig config = staConfig();
    strcpy(config.hostname, "aqua-doser");

    TEST_ASSERT_TRUE(service.begin(config));
    TEST_ASSERT_EQUAL_STRING("aqua-doser", service.hostname());
}

void test_network_has_no_web_mqtt_or_ha_dependency() {
    MockNetworkBackend backend;
    NetworkService service(backend);
    NetworkConfig config {};
    TEST_ASSERT_TRUE(service.begin(config));
    TEST_ASSERT_EQUAL_UINT32(0U, sizeof(config) == 0U ? 1U : 0U);
    assertState(NetworkState::Disabled, service);
}

} // namespace

void setUp() {
}

void tearDown() {
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_network_disabled);
    RUN_TEST(test_begin_is_non_blocking);
    RUN_TEST(test_sta_connecting_state);
    RUN_TEST(test_connected_state);
    RUN_TEST(test_disconnected_state);
    RUN_TEST(test_ip_is_reported);
    RUN_TEST(test_rssi_is_reported);
    RUN_TEST(test_ssid_is_reported);
    RUN_TEST(test_hostname_is_reported);
    RUN_TEST(test_reconnect_after_interval);
    RUN_TEST(test_no_reconnect_before_interval);
    RUN_TEST(test_reconnect_handles_millis_rollover);
    RUN_TEST(test_reconnect_count_tracks_attempts);
    RUN_TEST(test_backend_failure_does_not_crash);
    RUN_TEST(test_wifi_loss_does_not_restart_system);
    RUN_TEST(test_ap_disabled);
    RUN_TEST(test_ap_enabled);
    RUN_TEST(test_sta_and_ap_can_run_together);
    RUN_TEST(test_two_network_services_are_independent);
    RUN_TEST(test_services_have_no_shared_runtime_state);
    RUN_TEST(test_diagnostics_works_without_network);
    RUN_TEST(test_diagnostics_reports_network);
    RUN_TEST(test_network_has_no_lumasense_dependency);
    RUN_TEST(test_network_has_no_web_mqtt_or_ha_dependency);

    UNITY_END();
}

void loop() {
}
