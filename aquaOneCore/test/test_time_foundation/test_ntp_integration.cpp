#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Time/MonotonicClock.h"
#include "AquaCore/Time/NetworkNtpSync.h"
#include "AquaCore/Time/RtcWallClock.h"

using namespace AquaCore::Time;
namespace Network = AquaCore::Network;

namespace {

UtcDateTime makeUtc(uint16_t year, uint8_t month, uint8_t day) {
    UtcDateTime value {};
    value.year = year;
    value.month = month;
    value.day = day;
    return value;
}

class FakeClock final : public MonotonicClock {
public:
    uint64_t nowMilliseconds() const override { return nowMs; }
    uint64_t nowMs = static_cast<uint64_t>(UINT32_MAX) + 1000U;
};

class FakeRtcBus final : public RtcBus {
public:
    bool begin(int, int) override { return true; }
    bool readRegisters(uint8_t, uint8_t start, uint8_t* out, size_t length) override {
        if (out == nullptr || static_cast<size_t>(start) + length > sizeof(registers)) {
            return false;
        }
        memcpy(out, &registers[start], length);
        return true;
    }
    bool writeRegisters(uint8_t, uint8_t start, const uint8_t* in, size_t length) override {
        if (failWrites || in == nullptr ||
            static_cast<size_t>(start) + length > sizeof(registers)) {
            return false;
        }
        memcpy(&registers[start], in, length);
        return true;
    }
    static uint8_t bcd(uint8_t value) {
        return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
    }
    void setUtc(uint16_t year, uint8_t month, uint8_t day, bool valid = true) {
        registers[0] = 0U;
        registers[1] = 0U;
        registers[2] = 0U;
        registers[3] = bcd(1U);
        registers[4] = bcd(day);
        registers[5] = bcd(month);
        if (year >= 2100U) { registers[5] |= 0x80U; }
        registers[6] = bcd(static_cast<uint8_t>(year % 100U));
        registers[0x0F] = valid ? 0U : 0x80U;
    }
    bool failWrites = false;
    uint8_t registers[32] {};
};

class FakeNtpBackend final : public NtpBackend {
public:
    bool start(const char* const[NTP_MAX_SERVERS], uint8_t) override {
        ++starts;
        return startSucceeds;
    }
    NtpBackendResult poll(UtcDateTime& out) override {
        ++polls;
        out = utc;
        return result;
    }
    void stop() override { ++stops; }
    UtcDateTime utc = makeUtc(2026U, 1U, 2U);
    NtpBackendResult result = NtpBackendResult::Pending;
    bool startSucceeds = true;
    uint32_t starts = 0U;
    uint32_t polls = 0U;
    uint32_t stops = 0U;
};

class FakeNetworkBackend final : public Network::NetworkBackend {
public:
    bool applyRadioPolicy(Network::TriStateSetting, Network::TriStateSetting,
                          Network::WifiPowerSaveMode) override { return true; }
    bool setHostname(const char*) override { return true; }
    bool beginSta(const char*, const char*) override { return true; }
    Network::BackendStaState staState() const override { return state; }
    bool reconnectSta() override { return true; }
    bool disconnectSta() override { return true; }
    Network::IpAddress localIp() const override { return {}; }
    int32_t rssi() const override { return 0; }
    Network::NetworkDisconnectReason consumeDisconnectReason() override {
        return Network::NetworkDisconnectReason::None;
    }
    bool startAccessPoint(const char*, const char*) override { return true; }
    bool stopAccessPoint() override { return true; }
    Network::IpAddress accessPointIp() const override { return {}; }
    Network::BackendStaState state = Network::BackendStaState::Disconnected;
};

NtpConfig config() {
    NtpConfig value = NtpService::defaultConfig();
    value.timeoutMs = 50U;
    value.syncIntervalMs = 100U;
    return value;
}

struct Fixture {
    FakeRtcBus bus;
    RtcService rtc {bus, RtcConfig {}};
    RtcWallClock wall {rtc};
    FakeClock clock;
    FakeNtpBackend ntpBackend;
    NtpService ntp {rtc, ntpBackend, clock};
    FakeNetworkBackend networkBackend;
    Network::NetworkService network {networkBackend, clock};
    NetworkNtpSync sync {ntp, network};

    void connect() {
        Network::NetworkConfig value {};
        value.staEnabled = true;
        strcpy(value.ssid, "wifi");
        value.autoReconnect = false;
        TEST_ASSERT_TRUE(network.begin(value));
        networkBackend.state = Network::BackendStaState::Connected;
        network.update();
    }
};

void testValidRtcRemainsAvailableOffline() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.sync.update();
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767225600ULL, utc.secondsSinceUnixEpoch);
    TEST_ASSERT_EQUAL_UINT32(0U, f.ntpBackend.starts);
    TEST_ASSERT_FALSE(f.ntp.hasSyncResult());
}

void testConnectingAndDisconnectedDeferFirstAttemptUntilConnected() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    Network::NetworkConfig value {};
    value.staEnabled = true;
    strcpy(value.ssid, "wifi");
    value.autoReconnect = false;
    TEST_ASSERT_TRUE(f.network.begin(value));
    f.sync.update();
    f.network.update();
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(0U, f.ntpBackend.starts);
    f.networkBackend.state = Network::BackendStaState::Connected;
    f.network.update();
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
}

void testInvalidRtcRecoversAfterNtpWritesAndVerifies() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U, false);
    TEST_ASSERT_TRUE(f.rtc.begin());
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Unavailable),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    f.ntpBackend.result = NtpBackendResult::Success;
    f.sync.update();
    TEST_ASSERT_TRUE(f.ntp.isSyncInProgress());
    f.sync.update();
    TEST_ASSERT_TRUE(f.ntp.lastFetchSucceeded());
    TEST_ASSERT_TRUE(f.ntp.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767312000ULL, utc.secondsSinceUnixEpoch);
}

void testNtpCorrectionMovesRtcEitherWayWithoutMovingMonotonicClock() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    f.ntpBackend.result = NtpBackendResult::Success;
    const uint64_t before = f.clock.nowMilliseconds();
    f.sync.update();
    f.sync.update();
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767312000ULL, utc.secondsSinceUnixEpoch);
    TEST_ASSERT_EQUAL_UINT64(before, f.clock.nowMilliseconds());
    f.ntpBackend.utc = makeUtc(2025U, 1U, 1U);
    f.clock.nowMs += 100U;
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1735689600ULL, utc.secondsSinceUnixEpoch);
}

void testNtpFailureAndInvalidUtcPreserveValidRtc() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    f.ntpBackend.result = NtpBackendResult::Failure;
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_FALSE(f.ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(f.ntp.lastSyncSucceeded());
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767225600ULL, utc.secondsSinceUnixEpoch);
    f.clock.nowMs += 100U;
    f.ntpBackend.result = NtpBackendResult::Success;
    f.ntpBackend.utc = makeUtc(2200U, 1U, 1U);
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_FALSE(f.ntp.lastFetchSucceeded());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767225600ULL, utc.secondsSinceUnixEpoch);
}

void testRtcWriteFailureIsNotSynchronizationSuccess() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    // This fake fails before modifying RTC registers; partial writes do not
    // have a rollback guarantee in the hardware contract.
    f.bus.failWrites = true;
    f.ntpBackend.result = NtpBackendResult::Success;
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_TRUE(f.ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(f.ntp.lastSyncSucceeded());
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WallClockReadResult::Success),
                          static_cast<int>(f.wall.readUtc(utc)));
    TEST_ASSERT_EQUAL_UINT64(1767225600ULL, utc.secondsSinceUnixEpoch);
}

void testDisconnectCancelsAttemptAndRetryWaitsForDeadline() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
    f.networkBackend.state = Network::BackendStaState::Disconnected;
    f.network.update();
    f.sync.update();
    TEST_ASSERT_FALSE(f.ntp.isSyncInProgress());
    TEST_ASSERT_FALSE(f.ntp.lastSyncSucceeded());
    f.networkBackend.state = Network::BackendStaState::Connected;
    f.network.update();
    f.clock.nowMs += 99U;
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
    f.clock.nowMs += 1U;
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(2U, f.ntpBackend.starts);
}

void testMonotonicTimeoutAndPeriodicRetryAboveUint32() {
    Fixture f;
    f.bus.setUtc(2026U, 1U, 1U);
    TEST_ASSERT_TRUE(f.rtc.begin());
    TEST_ASSERT_TRUE(f.ntp.begin(config()));
    f.connect();
    f.sync.update();
    f.clock.nowMs += 49U;
    f.sync.update();
    TEST_ASSERT_TRUE(f.ntp.isSyncInProgress());
    f.clock.nowMs += 1U;
    f.sync.update();
    TEST_ASSERT_FALSE(f.ntp.isSyncInProgress());
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
    f.clock.nowMs += 99U;
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(1U, f.ntpBackend.starts);
    f.clock.nowMs += 1U;
    f.sync.update();
    f.sync.update();
    TEST_ASSERT_EQUAL_UINT32(2U, f.ntpBackend.starts);
}

void testLegacyNtpMillisWrapperExtendsRollover() {
    FakeRtcBus bus;
    bus.setUtc(2026U, 1U, 1U);
    RtcService rtc(bus, RtcConfig {});
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig value = config();
    value.timeoutMs = 30U;
    TEST_ASSERT_TRUE(ntp.begin(value, UINT32_MAX - 20U));
    TEST_ASSERT_TRUE(ntp.requestSync(true, UINT32_MAX - 10U));
    ntp.update(true, 18U);
    TEST_ASSERT_TRUE(ntp.isSyncInProgress());
    ntp.update(true, 19U);
    TEST_ASSERT_FALSE(ntp.isSyncInProgress());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
}

void testRtcRangeUsesGregorianCenturyLeapRule() {
    TEST_ASSERT_TRUE(RtcService::isValidUtc(2000U, 2U, 29U, 0U, 0U, 0U));
    TEST_ASSERT_FALSE(RtcService::isValidUtc(2100U, 2U, 29U, 0U, 0U, 0U));
    TEST_ASSERT_TRUE(RtcService::isValidUtc(2196U, 2U, 29U, 0U, 0U, 0U));
    TEST_ASSERT_FALSE(RtcService::isValidUtc(2200U, 1U, 1U, 0U, 0U, 0U));
}

} // namespace

void runNtpIntegrationTests() {
    RUN_TEST(testValidRtcRemainsAvailableOffline);
    RUN_TEST(testConnectingAndDisconnectedDeferFirstAttemptUntilConnected);
    RUN_TEST(testInvalidRtcRecoversAfterNtpWritesAndVerifies);
    RUN_TEST(testNtpCorrectionMovesRtcEitherWayWithoutMovingMonotonicClock);
    RUN_TEST(testNtpFailureAndInvalidUtcPreserveValidRtc);
    RUN_TEST(testRtcWriteFailureIsNotSynchronizationSuccess);
    RUN_TEST(testDisconnectCancelsAttemptAndRetryWaitsForDeadline);
    RUN_TEST(testMonotonicTimeoutAndPeriodicRetryAboveUint32);
    RUN_TEST(testLegacyNtpMillisWrapperExtendsRollover);
    RUN_TEST(testRtcRangeUsesGregorianCenturyLeapRule);
}
