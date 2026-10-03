#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Network/NetworkTypes.h"

using namespace AquaCore::Network;

namespace {

IpAddress makeIpAddress(
    uint8_t first,
    uint8_t second,
    uint8_t third,
    uint8_t fourth
) {
    IpAddress address {};
    address.octets[0] = first;
    address.octets[1] = second;
    address.octets[2] = third;
    address.octets[3] = fourth;
    return address;
}

class FakeNetworkBackend final : public NetworkBackend {
public:
    bool applyRadioPolicy(
        TriStateSetting,
        TriStateSetting,
        WifiPowerSaveMode
    ) override { return true; }

    bool setHostname(const char*) override { return true; }
    bool beginSta(const char*, const char*) override { return true; }
    BackendStaState staState() const override { return state; }
    bool reconnectSta() override { ++reconnectCalls; return true; }
    bool disconnectSta() override { return true; }
    IpAddress localIp() const override { return IpAddress {}; }
    int32_t rssi() const override { return -48; }
    NetworkDisconnectReason consumeDisconnectReason() override {
        return NetworkDisconnectReason::None;
    }
    bool startAccessPoint(const char*, const char*) override { return apStartSucceeds; }
    bool stopAccessPoint() override { return true; }
    IpAddress accessPointIp() const override {
        return makeIpAddress(192U, 168U, 4U, 1U);
    }

    BackendStaState state = BackendStaState::Disconnected;
    bool apStartSucceeds = true;
    uint32_t reconnectCalls = 0U;
};

NetworkConfig staConfig() {
    NetworkConfig config {};
    config.staEnabled = true;
    strcpy(config.ssid, "test-network");
    strcpy(config.password, "test-secret");
    config.autoReconnect = false;
    return config;
}

void testDisabledIsLegalAndDoesNotReconnect() {
    FakeNetworkBackend backend;
    NetworkService service(backend);

    NetworkConfig config {};
    TEST_ASSERT_TRUE(service.begin(config));
    service.update(10000U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Disabled),
        static_cast<uint8_t>(service.state())
    );
    TEST_ASSERT_FALSE(service.isStaEnabled());
    TEST_ASSERT_FALSE(service.isApEnabled());
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
}

void testStaStartAndBackendConnectionState() {
    FakeNetworkBackend backend;
    NetworkService service(backend);

    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Connecting),
        static_cast<uint8_t>(service.state())
    );

    backend.state = BackendStaState::Connected;
    service.update(10U);
    TEST_ASSERT_TRUE(service.isConnected());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Connected),
        static_cast<uint8_t>(service.state())
    );
    TEST_ASSERT_FALSE(service.ipAddress().isSet());
}

void testDisconnectedAndErrorRemainNetworkStates() {
    FakeNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));

    backend.state = BackendStaState::Disconnected;
    service.update(10U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Disconnected),
        static_cast<uint8_t>(service.state())
    );

    backend.state = BackendStaState::Error;
    service.update(20U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Error),
        static_cast<uint8_t>(service.state())
    );
    TEST_ASSERT_FALSE(service.isConnected());
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
}

void testStatusReflectsLatestServiceUpdate() {
    FakeNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Connecting),
        static_cast<uint8_t>(service.state())
    );

    backend.state = BackendStaState::Connected;
    service.update(50U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Connected),
        static_cast<uint8_t>(service.state())
    );

    backend.state = BackendStaState::Disconnected;
    service.update(60U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Disconnected),
        static_cast<uint8_t>(service.state())
    );
}

void testApAndStaAreIndependentAxes() {
    FakeNetworkBackend backend;
    NetworkService service(backend);
    NetworkConfig config = staConfig();
    config.apEnabled = true;
    strcpy(config.apSsid, "setup-network");

    TEST_ASSERT_TRUE(service.begin(config));
    backend.state = BackendStaState::Disconnected;
    service.update(10U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Disconnected),
        static_cast<uint8_t>(service.state())
    );
    TEST_ASSERT_TRUE(service.isApActive());

    FakeNetworkBackend apOnlyBackend;
    NetworkService apOnly(apOnlyBackend);
    NetworkConfig apOnlyConfig {};
    apOnlyConfig.apEnabled = true;
    strcpy(apOnlyConfig.apSsid, "setup-only");
    TEST_ASSERT_TRUE(apOnly.begin(apOnlyConfig));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetworkState::Idle),
        static_cast<uint8_t>(apOnly.state())
    );
    TEST_ASSERT_TRUE(apOnly.isApActive());
}

void testConnectedDoesNotPublishInternetOrCredentials() {
    FakeNetworkBackend backend;
    NetworkService service(backend);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.state = BackendStaState::Connected;
    service.update(10U);

    TEST_ASSERT_TRUE(service.isConnected());
    TEST_ASSERT_EQUAL_STRING("test-network", service.ssid());
    TEST_ASSERT_EQUAL_STRING("", service.accessPointSsid());
    // Public status API has no Internet-reachability or password field.
}

void setUp() {}
void tearDown() {}

void runTests() {
    RUN_TEST(testDisabledIsLegalAndDoesNotReconnect);
    RUN_TEST(testStaStartAndBackendConnectionState);
    RUN_TEST(testDisconnectedAndErrorRemainNetworkStates);
    RUN_TEST(testStatusReflectsLatestServiceUpdate);
    RUN_TEST(testApAndStaAreIndependentAxes);
    RUN_TEST(testConnectedDoesNotPublishInternetOrCredentials);
}

} // namespace

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    runTests();
    UNITY_END();
}

void loop() {}
#else
int main() {
    UNITY_BEGIN();
    runTests();
    return UNITY_END();
}
#endif
