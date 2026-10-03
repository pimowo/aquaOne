#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <string.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Network/NetworkStartup.h"
#include "AquaCore/Network/NetworkHealthProvider.h"
#include "AquaCore/Network/NetworkTypes.h"
#include "AquaCore/System/ApplicationRuntime.h"
#include "AquaCore/System/RuntimeStateCoordinator.h"
#include "AquaCore/Time/MonotonicClock.h"

using namespace AquaCore::Network;

namespace {

namespace System = AquaCore::System;

static_assert(!std::is_copy_constructible<NetworkService>::value,
    "NetworkService must keep its borrowed clock binding");

class FakeClock final : public AquaCore::Time::MonotonicClock {
public:
    explicit FakeClock(uint64_t start = 0U) : nowMs(start) {}
    uint64_t nowMilliseconds() const override { return nowMs; }
    void advance(uint64_t elapsed) { nowMs += elapsed; }
    uint64_t nowMs;
};

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
    ) override { return radioPolicySucceeds; }

    bool setHostname(const char*) override { return true; }
    bool beginSta(const char*, const char*) override { return staBeginSucceeds; }
    BackendStaState staState() const override { return state; }
    bool reconnectSta() override { ++reconnectCalls; return true; }
    bool disconnectSta() override { return true; }
    IpAddress localIp() const override { return IpAddress {}; }
    int32_t rssi() const override { return -48; }
    NetworkDisconnectReason consumeDisconnectReason() override {
        const NetworkDisconnectReason result = reason;
        reason = NetworkDisconnectReason::None;
        return result;
    }
    bool startAccessPoint(const char*, const char*) override { return apStartSucceeds; }
    bool stopAccessPoint() override { return true; }
    IpAddress accessPointIp() const override {
        return makeIpAddress(192U, 168U, 4U, 1U);
    }

    BackendStaState state = BackendStaState::Disconnected;
    bool apStartSucceeds = true;
    bool radioPolicySucceeds = true;
    bool staBeginSucceeds = true;
    NetworkDisconnectReason reason = NetworkDisconnectReason::None;
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

System::StartupStepResult startupSucceeded(void*) {
    return System::StartupStepResult::succeeded();
}

System::ApplicationPlan networkPlan(System::StartupParticipant& participant) {
    System::ApplicationPlan plan {};
    plan.earlySafeOutputs = {"early_safe", startupSucceeded, nullptr};
    plan.safetyGate = {"safety_gate", startupSucceeded, nullptr};
    plan.participants = &participant;
    plan.participantCount = 1U;
    return plan;
}

void expectRuntimeState(
    const System::ApplicationRuntime& runtime,
    System::HealthState health
) {
    const System::RuntimeStatus status = runtime.status();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::OperationalState::RUNNING),
        static_cast<int>(status.operational)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(health),
        static_cast<int>(status.health)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::SafetyState::CLEAR),
        static_cast<int>(status.safety)
    );
}

void testMonotonicRetryAboveUint32AndNoBusyLoop() {
    FakeClock clock(static_cast<uint64_t>(UINT32_MAX) + 1000U);
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    config.autoReconnect = true;
    config.reconnectIntervalMs = 100U;
    TEST_ASSERT_TRUE(service.begin(config));

    service.update();
    clock.advance(99U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
    clock.advance(1U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(1U, backend.reconnectCalls);
    service.update();
    service.update();
    TEST_ASSERT_EQUAL_UINT32(1U, backend.reconnectCalls);
}

void testLegacyMillisRolloverKeepsRetryTimeoutAndUptime() {
    FakeNetworkBackend retryBackend;
    NetworkService retryService(retryBackend);
    NetworkConfig retryConfig = staConfig();
    retryConfig.autoReconnect = true;
    retryConfig.reconnectIntervalMs = 100U;
    TEST_ASSERT_TRUE(retryService.begin(retryConfig));
    retryService.update(UINT32_MAX - 49U);
    retryService.update(49U);
    TEST_ASSERT_EQUAL_UINT32(0U, retryBackend.reconnectCalls);
    retryService.update(50U);
    TEST_ASSERT_EQUAL_UINT32(1U, retryBackend.reconnectCalls);
    retryService.update(50U);
    TEST_ASSERT_EQUAL_UINT32(1U, retryBackend.reconnectCalls);

    FakeNetworkBackend timeoutBackend;
    timeoutBackend.state = BackendStaState::Connecting;
    NetworkService timeoutService(timeoutBackend);
    NetworkConfig timeoutConfig = staConfig();
    timeoutConfig.connectTimeoutMs = 50U;
    TEST_ASSERT_TRUE(timeoutService.begin(timeoutConfig));
    timeoutService.update(UINT32_MAX - 24U);
    timeoutService.update(24U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Connecting),
        static_cast<int>(timeoutService.state())
    );
    timeoutService.update(25U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Disconnected),
        static_cast<int>(timeoutService.state())
    );

    FakeNetworkBackend uptimeBackend;
    NetworkService uptimeService(uptimeBackend);
    TEST_ASSERT_TRUE(uptimeService.begin(staConfig()));
    uptimeBackend.state = BackendStaState::Connected;
    uptimeService.update(UINT32_MAX - 24U);
    TEST_ASSERT_EQUAL_UINT32(50U, uptimeService.connectionUptimeMs(25U));
}

void testInjectedClockIgnoresLegacyCallerRollover() {
    FakeClock clock(static_cast<uint64_t>(UINT32_MAX) + 1000U);
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    config.autoReconnect = true;
    config.reconnectIntervalMs = 100U;
    TEST_ASSERT_TRUE(service.begin(config));
    service.update(UINT32_MAX - 1U);
    service.update(0U);
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
    clock.advance(100U);
    service.update(0U);
    TEST_ASSERT_EQUAL_UINT32(1U, backend.reconnectCalls);
}

void testMonotonicConnectTimeoutAboveUint32() {
    FakeClock clock(static_cast<uint64_t>(UINT32_MAX) + 500U);
    FakeNetworkBackend backend;
    backend.state = BackendStaState::Connecting;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    config.connectTimeoutMs = 50U;
    TEST_ASSERT_TRUE(service.begin(config));
    service.update();
    clock.advance(49U);
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Connecting),
        static_cast<int>(service.state())
    );
    clock.advance(1U);
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Disconnected),
        static_cast<int>(service.state())
    );
}

void testFastRetryUsesSameMonotonicClock() {
    FakeClock clock(static_cast<uint64_t>(UINT32_MAX) + 900U);
    FakeNetworkBackend backend;
    backend.reason = NetworkDisconnectReason::AssociationExpired;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    config.autoReconnect = true;
    config.reconnectIntervalMs = 100U;
    config.fastRetryEnabled = true;
    config.fastReconnectIntervalMs = 10U;
    TEST_ASSERT_TRUE(service.begin(config));
    service.update();
    clock.advance(9U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
    clock.advance(1U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(1U, backend.reconnectCalls);
}

void testFullWidthConnectionUptime() {
    FakeClock clock(static_cast<uint64_t>(UINT32_MAX) + 500U);
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    TEST_ASSERT_TRUE(service.begin(staConfig()));
    backend.state = BackendStaState::Connected;
    service.update();
    clock.advance(static_cast<uint64_t>(UINT32_MAX) + 2U);
    TEST_ASSERT_EQUAL_UINT64(
        static_cast<uint64_t>(UINT32_MAX) + 2U,
        service.connectionUptimeMs()
    );
}

void testStartupStepOutcomesForDisabledStaAndApOnly() {
    FakeClock clock;

    FakeNetworkBackend disabledBackend;
    disabledBackend.radioPolicySucceeds = false;
    NetworkService disabledService(disabledBackend, clock);
    NetworkConfig disabledConfig {};
    NetworkStartup disabled(disabledService, disabledConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupOutcome::DISABLED),
        static_cast<int>(NetworkStartup::run(&disabled).outcome())
    );

    FakeNetworkBackend staBackend;
    NetworkService staService(staBackend, clock);
    NetworkConfig sta = staConfig();
    NetworkStartup staStartup(staService, sta);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupOutcome::SUCCEEDED),
        static_cast<int>(NetworkStartup::run(&staStartup).outcome())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Connecting),
        static_cast<int>(staService.state())
    );

    FakeNetworkBackend apBackend;
    NetworkService apService(apBackend, clock);
    NetworkConfig ap {};
    ap.apEnabled = true;
    strcpy(ap.apSsid, "ap-only");
    NetworkStartup apStartup(apService, ap);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupOutcome::SUCCEEDED),
        static_cast<int>(NetworkStartup::run(&apStartup).outcome())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Idle),
        static_cast<int>(apService.state())
    );
}

void testDisabledNetworkStartupAndHealthAreOk() {
    FakeClock clock;
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkConfig config {};
    NetworkStartup startup(service, config);
    NetworkHealthProvider health(service);
    System::StartupParticipant participant = startup.participant();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupPhase::NETWORK_INIT),
        static_cast<int>(participant.phase)
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupRequirement::OPTIONAL),
        static_cast<int>(participant.requirement)
    );
    System::ApplicationRuntime runtime(
        networkPlan(participant), {nullptr, 0U}
    );
    const System::StartupReport& report = runtime.start();
    expectRuntimeState(runtime, System::HealthState::OK);
    TEST_ASSERT_EQUAL_UINT(0U, report.optionalFailureCount());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );
    const System::HealthProvider* providers[] = {&health};
    System::RuntimeStateCoordinator coordinator(providers, 1U, nullptr, 0U);
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
}

void testStaAndApOnlyStartupAreNonblocking() {
    FakeClock clock;
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    NetworkStartup startup(service, config);
    NetworkHealthProvider health(service);
    System::StartupParticipant participant = startup.participant();
    System::ApplicationRuntime runtime(networkPlan(participant), {nullptr, 0U});
    runtime.start();
    expectRuntimeState(runtime, System::HealthState::OK);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Connecting),
        static_cast<int>(service.state())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );

    FakeNetworkBackend apBackend;
    NetworkService apService(apBackend, clock);
    NetworkConfig apConfig {};
    apConfig.apEnabled = true;
    strcpy(apConfig.apSsid, "setup-only");
    NetworkStartup apStartup(apService, apConfig);
    NetworkHealthProvider apHealth(apService);
    System::StartupParticipant apParticipant = apStartup.participant();
    System::ApplicationRuntime apRuntime(
        networkPlan(apParticipant), {nullptr, 0U}
    );
    const System::StartupReport& apReport = apRuntime.start();
    expectRuntimeState(apRuntime, System::HealthState::OK);
    TEST_ASSERT_EQUAL_UINT(0U, apReport.optionalFailureCount());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Idle),
        static_cast<int>(apService.state())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(apHealth.healthContribution())
    );
}

void testHealthProviderMapsEveryLiveNetworkState() {
    FakeClock clock;
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkHealthProvider health(service);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );

    NetworkConfig apConfig {};
    apConfig.apEnabled = true;
    strcpy(apConfig.apSsid, "setup-only");
    TEST_ASSERT_TRUE(service.begin(apConfig));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );

    TEST_ASSERT_TRUE(service.begin(staConfig()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );
    backend.state = BackendStaState::Connected;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );
    backend.state = BackendStaState::Disconnected;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(health.healthContribution())
    );
    backend.state = BackendStaState::Error;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(health.healthContribution())
    );
}

void testRadioPolicyFailureIsNetworkErrorAndStartupFailure() {
    FakeClock clock;
    FakeNetworkBackend backend;
    backend.radioPolicySucceeds = false;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    NetworkStartup startup(service, config);
    NetworkHealthProvider health(service);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::StartupOutcome::FAILED),
        static_cast<int>(NetworkStartup::run(&startup).outcome())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Error),
        static_cast<int>(service.state())
    );
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(health.healthContribution())
    );
}

void testOptionalFailureHandsOffAndLiveRecoveryKeepsReportHistory() {
    FakeClock clock;
    FakeNetworkBackend backend;
    backend.staBeginSucceeds = false;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    NetworkStartup startup(service, config);
    NetworkHealthProvider health(service);
    System::StartupParticipant participant = startup.participant();
    System::ApplicationRuntime runtime(networkPlan(participant), {nullptr, 0U});
    const System::StartupReport& report = runtime.start();

    expectRuntimeState(runtime, System::HealthState::DEGRADED);
    TEST_ASSERT_EQUAL_UINT(1U, report.optionalFailureCount());
    backend.state = BackendStaState::Connected;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(NetworkState::Error),
        static_cast<int>(service.state())
    );
    const System::HealthProvider* providers[] = {&health};
    System::RuntimeStateCoordinator coordinator(providers, 1U, nullptr, 0U);
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    expectRuntimeState(runtime, System::HealthState::DEGRADED);

    backend.staBeginSucceeds = true;
    TEST_ASSERT_TRUE(service.begin(config));
    backend.state = BackendStaState::Connected;
    clock.advance(20U);
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::RuntimeStateRefreshResult::REFRESHED),
        static_cast<int>(coordinator.refresh())
    );
    expectRuntimeState(runtime, System::HealthState::OK);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(report.finalStatus().health)
    );
}

void testReconnectRecoveryChangesLiveHealthWithoutSafetyLock() {
    FakeClock clock(100U);
    FakeNetworkBackend backend;
    NetworkService service(backend, clock);
    NetworkConfig config = staConfig();
    config.autoReconnect = true;
    config.reconnectIntervalMs = 100U;
    TEST_ASSERT_TRUE(service.begin(config));
    NetworkHealthProvider health(service);
    backend.state = BackendStaState::Connected;
    service.update();
    backend.state = BackendStaState::Disconnected;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::DEGRADED),
        static_cast<int>(health.healthContribution())
    );
    clock.advance(99U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(0U, backend.reconnectCalls);
    clock.advance(1U);
    service.update();
    TEST_ASSERT_EQUAL_UINT32(1U, backend.reconnectCalls);
    backend.state = BackendStaState::Connected;
    service.update();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(System::HealthState::OK),
        static_cast<int>(health.healthContribution())
    );
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
    RUN_TEST(testMonotonicRetryAboveUint32AndNoBusyLoop);
    RUN_TEST(testLegacyMillisRolloverKeepsRetryTimeoutAndUptime);
    RUN_TEST(testInjectedClockIgnoresLegacyCallerRollover);
    RUN_TEST(testMonotonicConnectTimeoutAboveUint32);
    RUN_TEST(testFastRetryUsesSameMonotonicClock);
    RUN_TEST(testFullWidthConnectionUptime);
    RUN_TEST(testStartupStepOutcomesForDisabledStaAndApOnly);
    RUN_TEST(testDisabledNetworkStartupAndHealthAreOk);
    RUN_TEST(testStaAndApOnlyStartupAreNonblocking);
    RUN_TEST(testHealthProviderMapsEveryLiveNetworkState);
    RUN_TEST(testRadioPolicyFailureIsNetworkErrorAndStartupFailure);
    RUN_TEST(testOptionalFailureHandsOffAndLiveRecoveryKeepsReportHistory);
    RUN_TEST(testReconnectRecoveryChangesLiveHealthWithoutSafetyLock);
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
