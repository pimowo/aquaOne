#include <Arduino.h>
#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "AquaCore/Time/EuropeWarsawTimeService.h"
#include "AquaCore/Time/NtpService.h"
#include "AquaCore/Time/ResilientTimeService.h"
#include "AquaCore/Time/RtcService.h"

using namespace AquaCore::Time;

void setUp() {
}

void tearDown() {
}

namespace {

class FakeRtcBus final : public RtcBus {
public:
    bool begin(int sdaPin, int sclPin) override {
        capturedSdaPin = sdaPin;
        capturedSclPin = sclPin;
        return beginResult;
    }

    bool readRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        uint8_t* output,
        size_t length
    ) override {
        ++readCalls;

        if (
            failNextRead ||
            deviceAddress != expectedAddress ||
            output == nullptr
        ) {
            failNextRead = false;
            return false;
        }

        memcpy(output, &registers[startRegister], length);
        return true;
    }

    bool writeRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        const uint8_t* input,
        size_t length
    ) override {
        ++writeCalls;

        if (
            deviceAddress != expectedAddress ||
            input == nullptr ||
            (failWriteOnCall > 0 &&
             writeCalls == failWriteOnCall)
        ) {
            return false;
        }

        if (!discardWrites) {
            memcpy(&registers[startRegister], input, length);
        }

        return true;
    }

    uint8_t registers[256] {};
    uint8_t expectedAddress = 0x68U;
    bool beginResult = true;
    bool failNextRead = false;
    bool discardWrites = false;
    int failWriteOnCall = -1;
    int readCalls = 0;
    int writeCalls = 0;
    int capturedSdaPin = -1;
    int capturedSclPin = -1;
};

class FakeNtpBackend final : public NtpBackend {
public:
    bool start(
        const char* const[NTP_MAX_SERVERS],
        uint8_t serverCount
    ) override {
        ++startCalls;
        capturedServerCount = serverCount;
        return startResult;
    }

    NtpBackendResult poll(UtcDateTime& output) override {
        ++pollCalls;
        output = utc;
        return result;
    }

    void stop() override {
        ++stopCalls;
    }

    bool startResult = true;
    NtpBackendResult result = NtpBackendResult::Pending;
    UtcDateTime utc {};
    uint16_t startCalls = 0U;
    uint16_t pollCalls = 0U;
    uint16_t stopCalls = 0U;
    uint8_t capturedServerCount = 0U;
};

RtcConfig rtcConfig() {
    RtcConfig config {};
    config.sdaPin = 32;
    config.sclPin = 33;
    config.i2cAddress = 0x68U;
    return config;
}

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4) | (value % 10U)
    );
}

void fillRegisters(
    FakeRtcBus& bus,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    uint8_t status = 0U
) {
    bus.registers[0x00] = toBcd(second);
    bus.registers[0x01] = toBcd(minute);
    bus.registers[0x02] = toBcd(hour);
    bus.registers[0x03] = toBcd(1U);
    bus.registers[0x04] = toBcd(day);
    bus.registers[0x05] = toBcd(month);

    if (year >= 2100U) {
        bus.registers[0x05] |= 0x80U;
    }

    bus.registers[0x06] =
        toBcd(static_cast<uint8_t>(year % 100U));
    bus.registers[0x0F] = status;
}

UtcDateTime utcAt(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    UtcDateTime result {};
    result.year = year;
    result.month = month;
    result.day = day;
    result.hour = hour;
    result.minute = minute;
    result.second = second;
    return result;
}

void assertDateTime(
    const LocalTime& value,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    TEST_ASSERT_TRUE(value.valid);
    TEST_ASSERT_EQUAL_UINT16(year, value.year);
    TEST_ASSERT_EQUAL_UINT8(month, value.month);
    TEST_ASSERT_EQUAL_UINT8(day, value.day);
    TEST_ASSERT_EQUAL_UINT8(hour, value.hour);
    TEST_ASSERT_EQUAL_UINT8(minute, value.minute);
    TEST_ASSERT_EQUAL_UINT8(second, value.second);
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<uint16_t>(hour) * 60U + minute,
        value.minuteOfDay
    );
}

void test_valid_rtc_read() {
    FakeRtcBus bus;
    fillRegisters(bus, 2028U, 2U, 29U, 23U, 58U, 59U);
    RtcService rtc(bus, rtcConfig());

    TEST_ASSERT_TRUE(rtc.begin());
    assertDateTime(
        rtc.read(),
        2028U, 2U, 29U, 23U, 58U, 59U
    );
    TEST_ASSERT_TRUE(rtc.isValid());
    TEST_ASSERT_EQUAL_INT(32, bus.capturedSdaPin);
    TEST_ASSERT_EQUAL_INT(33, bus.capturedSclPin);
}

void test_osf_at_boot_marks_rtc_invalid() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U, 0x80U);
    RtcService rtc(bus, rtcConfig());

    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());
    TEST_ASSERT_FALSE(rtc.read().valid);
}

void test_osf_appearing_at_runtime_invalidates_rtc() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_TRUE(rtc.read().valid);

    bus.registers[0x0F] |= 0x80U;

    TEST_ASSERT_FALSE(rtc.read().valid);
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_invalid_bcd_is_rejected() {
    uint8_t data[7] {
        0x1AU, 0x00U, 0x12U, 0x01U,
        0x08U, 0x09U, 0x26U
    };
    LocalTime value {};

    TEST_ASSERT_FALSE(
        RtcService::decodeDateTimeRegisters(data, value)
    );
    TEST_ASSERT_FALSE(value.valid);
}

void test_invalid_date_is_rejected() {
    TEST_ASSERT_FALSE(
        RtcService::isValidUtc(
            2026U, 4U, 31U, 12U, 0U, 0U
        )
    );
}

void test_leap_year_february_29_is_valid() {
    TEST_ASSERT_TRUE(
        RtcService::isValidUtc(
            2028U, 2U, 29U, 12U, 0U, 0U
        )
    );
}

void test_non_leap_february_29_is_rejected() {
    TEST_ASSERT_FALSE(
        RtcService::isValidUtc(
            2027U, 2U, 29U, 12U, 0U, 0U
        )
    );
}

void test_valid_set_utc_is_written_and_verified() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());

    TEST_ASSERT_TRUE(
        rtc.setUtc(2028U, 2U, 29U, 21U, 45U, 37U)
    );

    assertDateTime(
        rtc.read(),
        2028U, 2U, 29U, 21U, 45U, 37U
    );
}

void test_set_utc_clears_osf() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U, 0x8BU);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());

    TEST_ASSERT_TRUE(
        rtc.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_EQUAL_HEX8(0x0BU, bus.registers[0x0F]);
    TEST_ASSERT_TRUE(rtc.isValid());
}

void test_i2c_failure_returns_false() {
    FakeRtcBus beginBus;
    beginBus.beginResult = false;
    RtcService beginRtc(beginBus, rtcConfig());
    TEST_ASSERT_FALSE(beginRtc.begin());

    FakeRtcBus readBus;
    readBus.failNextRead = true;
    RtcService readRtc(readBus, rtcConfig());
    TEST_ASSERT_FALSE(readRtc.begin());
}

void test_acknowledged_unapplied_write_is_rejected() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    bus.discardWrites = true;

    TEST_ASSERT_FALSE(
        rtc.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_ntp_request_starts_non_blocking() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);

    TEST_ASSERT_TRUE(ntp.begin(100U));
    TEST_ASSERT_TRUE(ntp.requestSync(true, 200U));
    TEST_ASSERT_TRUE(ntp.isSyncInProgress());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, backend.pollCalls);
}

void test_ntp_timeout_finishes_as_failure() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.timeoutMs = 100U;
    TEST_ASSERT_TRUE(ntp.begin(config, 0U));
    TEST_ASSERT_TRUE(ntp.requestSync(true, 500U));

    ntp.update(true, 599U);
    TEST_ASSERT_TRUE(ntp.isSyncInProgress());
    ntp.update(true, 600U);

    TEST_ASSERT_FALSE(ntp.isSyncInProgress());
    TEST_ASSERT_TRUE(ntp.hasSyncResult());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.stopCalls);
}

void test_ntp_success_writes_rtc() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2028U, 2U, 29U, 21U, 45U, 37U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.lastSyncSucceeded());
    assertDateTime(
        rtc.read(),
        2028U, 2U, 29U, 21U, 45U, 37U
    );
}

void test_ntp_failure_does_not_alter_rtc() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 11U, 12U);
    uint8_t before[7] {};
    memcpy(before, bus.registers, sizeof(before));
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Failure;
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_MEMORY(
        before,
        bus.registers,
        sizeof(before)
    );
}

void test_set_utc_failure_makes_sync_fail() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 8U, 12U, 34U, 56U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());
    bus.failWriteOnCall = 1;

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.hasSyncResult());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_parallel_sync_is_rejected() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    TEST_ASSERT_FALSE(ntp.requestSync(true, 101U));
    TEST_ASSERT_FALSE(ntp.requestPeriodicSync(true, 102U));
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
}

void test_ntp_timeout_handles_millis_overflow() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.timeoutMs = 1000U;
    TEST_ASSERT_TRUE(ntp.begin(config, UINT32_MAX - 1000U));
    TEST_ASSERT_TRUE(
        ntp.requestSync(true, UINT32_MAX - 500U)
    );

    ntp.update(true, 498U);
    TEST_ASSERT_TRUE(ntp.isSyncInProgress());
    ntp.update(true, 499U);
    TEST_ASSERT_FALSE(ntp.isSyncInProgress());
}

void test_ntp_keeps_utc_without_timezone_offset() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 7U, 1U, 12U, 15U, 30U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    assertDateTime(
        rtc.read(),
        2026U, 7U, 1U, 12U, 15U, 30U
    );
}

void test_europe_warsaw_winter_conversion_is_unchanged() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 15U, 12U, 30U, 0U);
    RtcService rtc(bus, rtcConfig());
    EuropeWarsawTimeService time(rtc);

    TEST_ASSERT_TRUE(time.begin());
    assertDateTime(
        time.current(),
        2026U, 1U, 15U, 13U, 30U, 0U
    );
}

void test_dst_start_boundary_is_unchanged() {
    FakeRtcBus bus;
    fillRegisters(bus, 2024U, 3U, 31U, 0U, 59U, 0U);
    RtcService rtc(bus, rtcConfig());
    EuropeWarsawTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin());
    assertDateTime(
        time.current(),
        2024U, 3U, 31U, 1U, 59U, 0U
    );

    fillRegisters(bus, 2024U, 3U, 31U, 1U, 0U, 0U);
    assertDateTime(
        time.now(),
        2024U, 3U, 31U, 3U, 0U, 0U
    );
}

void test_dst_end_boundary_is_unchanged() {
    FakeRtcBus bus;
    fillRegisters(bus, 2024U, 10U, 27U, 0U, 59U, 0U);
    RtcService rtc(bus, rtcConfig());
    EuropeWarsawTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin());
    assertDateTime(
        time.current(),
        2024U, 10U, 27U, 2U, 59U, 0U
    );

    fillRegisters(bus, 2024U, 10U, 27U, 1U, 0U, 0U);
    assertDateTime(
        time.now(),
        2024U, 10U, 27U, 2U, 0U, 0U
    );
}

void test_invalid_rtc_remains_invalid() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 2U, 28U, 12U, 0U, 0U);
    bus.registers[0x04] = 0x30U;
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());

    TEST_ASSERT_FALSE(rtc.read().valid);
    TEST_ASSERT_FALSE(rtc.isValid());
    TEST_ASSERT_FALSE(rtc.read().valid);
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_ntp_recovers_invalid_rtc() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U, 0x80U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());

    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 8U, 11U, 22U, 33U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());
    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.lastSyncSucceeded());
    TEST_ASSERT_TRUE(rtc.isValid());
    assertDateTime(
        rtc.read(),
        2026U, 9U, 8U, 11U, 22U, 33U
    );
}

void test_resilient_boot_valid_rtc_is_healthy_and_valid() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);

    TEST_ASSERT_TRUE(time.begin(1000U));
    TEST_ASSERT_TRUE(time.isRtcHealthy());
    TEST_ASSERT_TRUE(time.isValid());
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(3U, time.consecutiveSuccesses());
    assertDateTime(time.now(1000U), 2026U, 9U, 11U, 10U, 0U, 0U);
}

void test_resilient_boot_failed_rtc_is_unhealthy_and_invalid() {
    FakeRtcBus bus;
    bus.failNextRead = true;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);

    TEST_ASSERT_FALSE(time.begin(1000U));
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_FALSE(time.isValid());
    TEST_ASSERT_EQUAL_UINT8(1U, time.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveSuccesses());
    TEST_ASSERT_FALSE(time.now(1000U).valid);
}

void test_resilient_single_failure_maintains_healthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);

    TEST_ASSERT_TRUE(time.isRtcHealthy());
    TEST_ASSERT_TRUE(time.isValid());
    TEST_ASSERT_EQUAL_UINT8(1U, time.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveSuccesses());
}

void test_resilient_two_failures_maintain_healthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);

    TEST_ASSERT_TRUE(time.isRtcHealthy());
    TEST_ASSERT_TRUE(time.isValid());
    TEST_ASSERT_EQUAL_UINT8(2U, time.consecutiveFailures());
}

void test_resilient_three_failures_drop_healthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    bus.failNextRead = true;
    time.poll(3000U);

    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_EQUAL_UINT8(3U, time.consecutiveFailures());
}

void test_resilient_cache_remains_valid_when_rtc_unhealthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    bus.failNextRead = true;
    time.poll(3000U);

    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_TRUE(time.isValid());
}

void test_resilient_interpolation_works_when_rtc_unhealthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 12U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    bus.failNextRead = true;
    time.poll(3000U);

    TEST_ASSERT_FALSE(time.isRtcHealthy());
    assertDateTime(time.now(15000U), 2026U, 9U, 11U, 12U, 0U, 15U);
}

void test_resilient_recovery_requires_exact_threshold_successes() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    bus.failNextRead = true;
    time.poll(3000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 4U);
    time.poll(4000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_EQUAL_UINT8(1U, time.consecutiveSuccesses());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 5U);
    time.poll(5000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_EQUAL_UINT8(2U, time.consecutiveSuccesses());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 6U);
    time.poll(6000U);
    TEST_ASSERT_TRUE(time.isRtcHealthy());
    TEST_ASSERT_EQUAL_UINT8(3U, time.consecutiveSuccesses());
}

void test_resilient_failure_during_recovery_resets_success_counter() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    bus.failNextRead = true;
    time.poll(3000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 4U);
    time.poll(4000U);
    TEST_ASSERT_EQUAL_UINT8(1U, time.consecutiveSuccesses());

    bus.failNextRead = true;
    time.poll(5000U);
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveSuccesses());
    TEST_ASSERT_FALSE(time.isRtcHealthy());
}

void test_resilient_success_during_failure_sequence_resets_failure_counter() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    TEST_ASSERT_EQUAL_UINT8(2U, time.consecutiveFailures());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 3U);
    time.poll(3000U);
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveFailures());
    TEST_ASSERT_TRUE(time.isRtcHealthy());
}

void test_resilient_successful_poll_refreshes_cache() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 5U);
    time.poll(5000U);

    TEST_ASSERT_EQUAL_UINT32(5000U, time.cachedAtMs());
    assertDateTime(time.now(5000U), 2026U, 9U, 11U, 10U, 0U, 5U);
}

void test_resilient_invalid_rtc_reading_treated_as_failure() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.registers[0x0F] = 0x80U; // OSF set
    time.poll(1000U);

    TEST_ASSERT_EQUAL_UINT8(1U, time.consecutiveFailures());
}

void test_resilient_interpolation_advances_65_seconds() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    assertDateTime(time.now(65000U), 2026U, 9U, 11U, 10U, 1U, 5U);
}

void test_resilient_handles_millis_rollover() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(UINT32_MAX - 5000U));

    const LocalTime result = time.now(5000U);
    assertDateTime(result, 2026U, 9U, 11U, 10U, 0U, 10U);
}

void test_resilient_sync_utc_updates_cache() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    LocalTime newUtc {};
    newUtc.valid = true;
    newUtc.year = 2028U;
    newUtc.month = 2U;
    newUtc.day = 29U;
    newUtc.hour = 23U;
    newUtc.minute = 59U;
    newUtc.second = 50U;
    newUtc.minuteOfDay = 23U * 60U + 59U;

    time.syncUtc(newUtc, 10000U);

    assertDateTime(time.now(10000U), 2028U, 2U, 29U, 23U, 59U, 50U);
    assertDateTime(time.now(25000U), 2028U, 3U, 1U, 0U, 0U, 5U);
}

void test_resilient_sync_utc_does_not_mark_hardware_healthy() {
    FakeRtcBus bus;
    bus.failNextRead = true;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_FALSE(time.begin(0U));
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    LocalTime newUtc {};
    newUtc.valid = true;
    newUtc.year = 2026U;
    newUtc.month = 9U;
    newUtc.day = 11U;
    newUtc.hour = 12U;
    newUtc.minute = 0U;
    newUtc.second = 0U;
    newUtc.minuteOfDay = 720U;

    time.syncUtc(newUtc, 1000U);

    TEST_ASSERT_TRUE(time.isValid());
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    assertDateTime(time.now(1000U), 2026U, 9U, 11U, 12U, 0U, 0U);
}

void test_resilient_two_instances_are_isolated() {
    FakeRtcBus firstBus;
    FakeRtcBus secondBus;
    fillRegisters(firstBus, 2026U, 9U, 11U, 10U, 0U, 0U);
    fillRegisters(secondBus, 2026U, 9U, 11U, 15U, 30U, 0U);

    RtcService firstRtc(firstBus, rtcConfig());
    RtcService secondRtc(secondBus, rtcConfig());

    ResilientTimeService firstTime(firstRtc);
    ResilientTimeService secondTime(secondRtc);

    TEST_ASSERT_TRUE(firstTime.begin(0U));
    TEST_ASSERT_TRUE(secondTime.begin(0U));

    firstBus.failNextRead = true;
    firstTime.poll(1000U);
    firstBus.failNextRead = true;
    firstTime.poll(2000U);
    firstBus.failNextRead = true;
    firstTime.poll(3000U);

    TEST_ASSERT_FALSE(firstTime.isRtcHealthy());
    TEST_ASSERT_TRUE(secondTime.isRtcHealthy());
    TEST_ASSERT_EQUAL_UINT8(3U, firstTime.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(0U, secondTime.consecutiveFailures());

    assertDateTime(firstTime.now(3000U), 2026U, 9U, 11U, 10U, 0U, 3U);
    assertDateTime(secondTime.now(3000U), 2026U, 9U, 11U, 15U, 30U, 3U);
}

void test_resilient_counters_do_not_overflow() {
    FakeRtcBus bus;
    bus.failNextRead = true;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_FALSE(time.begin(0U));

    for (int i = 0; i < 300; ++i) {
        bus.failNextRead = true;
        time.poll(static_cast<uint32_t>(i * 1000));
    }
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, time.consecutiveFailures());

    for (int i = 0; i < 300; ++i) {
        fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
        time.poll(static_cast<uint32_t>(300000 + i * 1000));
    }
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, time.consecutiveSuccesses());
    TEST_ASSERT_EQUAL_UINT8(0U, time.consecutiveFailures());
}

void test_resilient_custom_failure_threshold() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.failureThreshold = 5U;
    config.recoveryThreshold = 2U;
    ResilientTimeService time(rtc, config);
    TEST_ASSERT_TRUE(time.begin(0U));

    for (uint32_t i = 1U; i <= 4U; ++i) {
        bus.failNextRead = true;
        time.poll(i * 1000U);
        TEST_ASSERT_TRUE(time.isRtcHealthy());
    }

    bus.failNextRead = true;
    time.poll(5000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());
}

void test_resilient_sync_utc_rejects_invalid_date_fields() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    // month = 13
    LocalTime badMonth {};
    badMonth.valid = true;
    badMonth.year = 2026U; badMonth.month = 13U; badMonth.day = 1U;
    time.syncUtc(badMonth, 1000U);
    assertDateTime(time.now(1000U), 2026U, 9U, 11U, 10U, 0U, 1U); // cache nie zmieniony

    // hour = 25
    LocalTime badHour {};
    badHour.valid = true;
    badHour.year = 2026U; badHour.month = 9U; badHour.day = 11U; badHour.hour = 25U;
    time.syncUtc(badHour, 2000U);
    assertDateTime(time.now(2000U), 2026U, 9U, 11U, 10U, 0U, 2U);

    // 31 lutego
    LocalTime badFeb31 {};
    badFeb31.valid = true;
    badFeb31.year = 2026U; badFeb31.month = 2U; badFeb31.day = 31U;
    time.syncUtc(badFeb31, 3000U);
    assertDateTime(time.now(3000U), 2026U, 9U, 11U, 10U, 0U, 3U);

    // 29 lutego w roku nieprzestępnym
    LocalTime badFeb29NonLeap {};
    badFeb29NonLeap.valid = true;
    badFeb29NonLeap.year = 2027U; badFeb29NonLeap.month = 2U; badFeb29NonLeap.day = 29U;
    time.syncUtc(badFeb29NonLeap, 4000U);
    assertDateTime(time.now(4000U), 2026U, 9U, 11U, 10U, 0U, 4U);

    // poprawny 29 lutego w roku przestępnym
    LocalTime goodFeb29Leap {};
    goodFeb29Leap.valid = true;
    goodFeb29Leap.year = 2028U; goodFeb29Leap.month = 2U; goodFeb29Leap.day = 29U;
    goodFeb29Leap.hour = 12U; goodFeb29Leap.minute = 0U; goodFeb29Leap.second = 0U;
    time.syncUtc(goodFeb29Leap, 5000U);
    assertDateTime(time.now(5000U), 2028U, 2U, 29U, 12U, 0U, 0U);
}

void test_resilient_normalizes_zero_thresholds_to_one() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.failureThreshold = 0U;
    config.recoveryThreshold = 0U;
    ResilientTimeService time(rtc, config);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy()); // próg 1 zadziałał natychmiast

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 2U);
    time.poll(2000U);
    TEST_ASSERT_TRUE(time.isRtcHealthy()); // próg recovery 1 zadziałał natychmiast
}

void test_resilient_calendar_transitions() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 31U, 23U, 59U, 59U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);

    // 2026-01-31 23:59:59 + 1s -> 2026-02-01 00:00:00
    TEST_ASSERT_TRUE(time.begin(0U));
    assertDateTime(time.now(1000U), 2026U, 2U, 1U, 0U, 0U, 0U);

    // 2026-02-28 23:59:59 + 1s -> 2026-03-01 00:00:00 (non-leap)
    LocalTime feb28NonLeap {};
    feb28NonLeap.valid = true;
    feb28NonLeap.year = 2026U; feb28NonLeap.month = 2U; feb28NonLeap.day = 28U;
    feb28NonLeap.hour = 23U; feb28NonLeap.minute = 59U; feb28NonLeap.second = 59U;
    time.syncUtc(feb28NonLeap, 10000U);
    assertDateTime(time.now(11000U), 2026U, 3U, 1U, 0U, 0U, 0U);

    // 2028-02-28 23:59:59 + 1s -> 2028-02-29 00:00:00 (leap)
    LocalTime feb28Leap {};
    feb28Leap.valid = true;
    feb28Leap.year = 2028U; feb28Leap.month = 2U; feb28Leap.day = 28U;
    feb28Leap.hour = 23U; feb28Leap.minute = 59U; feb28Leap.second = 59U;
    time.syncUtc(feb28Leap, 20000U);
    assertDateTime(time.now(21000U), 2028U, 2U, 29U, 0U, 0U, 0U);

    // 2028-02-29 23:59:59 + 1s -> 2028-03-01 00:00:00 (leap end)
    LocalTime feb29Leap {};
    feb29Leap.valid = true;
    feb29Leap.year = 2028U; feb29Leap.month = 2U; feb29Leap.day = 29U;
    feb29Leap.hour = 23U; feb29Leap.minute = 59U; feb29Leap.second = 59U;
    time.syncUtc(feb29Leap, 30000U);
    assertDateTime(time.now(31000U), 2028U, 3U, 1U, 0U, 0U, 0U);

    // 2026-12-31 23:59:59 + 1s -> 2027-01-01 00:00:00 (year boundary)
    LocalTime dec31 {};
    dec31.valid = true;
    dec31.year = 2026U; dec31.month = 12U; dec31.day = 31U;
    dec31.hour = 23U; dec31.minute = 59U; dec31.second = 59U;
    time.syncUtc(dec31, 40000U);
    assertDateTime(time.now(41000U), 2027U, 1U, 1U, 0U, 0U, 0U);
}

void test_resilient_custom_recovery_threshold() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.failureThreshold = 2U;
    config.recoveryThreshold = 1U;
    ResilientTimeService time(rtc, config);
    TEST_ASSERT_TRUE(time.begin(0U));

    bus.failNextRead = true;
    time.poll(1000U);
    bus.failNextRead = true;
    time.poll(2000U);
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 3U);
    time.poll(3000U);
    TEST_ASSERT_TRUE(time.isRtcHealthy());
}

void test_ntp_fetch_success_and_rtc_write_success() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2028U, 2U, 29U, 21U, 45U, 37U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.lastFetchSucceeded());
    TEST_ASSERT_TRUE(ntp.lastSyncSucceeded());

    UtcDateTime output {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(output));
    TEST_ASSERT_EQUAL_UINT16(2028U, output.year);
    TEST_ASSERT_EQUAL_UINT8(2U, output.month);
    TEST_ASSERT_EQUAL_UINT8(29U, output.day);
    TEST_ASSERT_EQUAL_UINT8(21U, output.hour);
    TEST_ASSERT_EQUAL_UINT8(45U, output.minute);
    TEST_ASSERT_EQUAL_UINT8(37U, output.second);
}

void test_ntp_fetch_success_even_when_rtc_write_fails() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 8U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 8U, 12U, 34U, 56U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());
    bus.failWriteOnCall = 1; // Zapis do DS3231 zawiedzie

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.hasSyncResult());
    TEST_ASSERT_TRUE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded()); // Pełny sync fail przez błąd RTC

    UtcDateTime output {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(output));
    TEST_ASSERT_EQUAL_UINT16(2026U, output.year);
    TEST_ASSERT_EQUAL_UINT8(9U, output.month);
    TEST_ASSERT_EQUAL_UINT8(8U, output.day);
    TEST_ASSERT_EQUAL_UINT8(12U, output.hour);
    TEST_ASSERT_EQUAL_UINT8(34U, output.minute);
    TEST_ASSERT_EQUAL_UINT8(56U, output.second);
}

void test_ntp_take_received_utc_consumes_result_exactly_once() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 15U, 0U, 0U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    UtcDateTime first {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(first));
    TEST_ASSERT_EQUAL_UINT8(15U, first.hour);

    UtcDateTime second {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(second));
    TEST_ASSERT_EQUAL_UINT8(0U, second.hour);
}

void test_ntp_invalid_utc_does_not_set_fetch_success() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2027U, 2U, 29U, 12U, 0U, 0U); // 29 lutego w roku nieprzestępnym
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_ntp_backend_failure_resets_fetch_success() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Failure;
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_ntp_timeout_resets_fetch_success() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Pending;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.timeoutMs = 100U;
    TEST_ASSERT_TRUE(ntp.begin(config, 0U));

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 201U);

    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_ntp_wifi_loss_resets_fetch_success() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Pending;
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin(0U));

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(false, 110U);

    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_ntp_subsequent_sync_produces_new_consumable_utc() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.syncIntervalMs = 1000U;
    TEST_ASSERT_TRUE(ntp.begin(config, 0U));

    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 10U, 0U, 0U);
    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    UtcDateTime first {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(first));
    TEST_ASSERT_EQUAL_UINT8(10U, first.hour);

    backend.utc = utcAt(2026U, 9U, 11U, 11U, 0U, 0U);
    TEST_ASSERT_TRUE(ntp.requestSync(true, 2000U));
    ntp.update(true, 2001U);

    UtcDateTime second {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(second));
    TEST_ASSERT_EQUAL_UINT8(11U, second.hour);
}

void test_ntp_two_fetches_without_consumption_keeps_latest() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.syncIntervalMs = 1000U;
    TEST_ASSERT_TRUE(ntp.begin(config, 0U));

    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 10U, 0U, 0U);
    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    backend.utc = utcAt(2026U, 9U, 11U, 12U, 0U, 0U);
    TEST_ASSERT_TRUE(ntp.requestSync(true, 2000U));
    ntp.update(true, 2001U);

    UtcDateTime output {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(output));
    TEST_ASSERT_EQUAL_UINT8(12U, output.hour);
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_ntp_pending_preserved_if_subsequent_attempt_fails() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.syncIntervalMs = 1000U;
    TEST_ASSERT_TRUE(ntp.begin(config, 0U));

    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 10U, 0U, 0U);
    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    backend.result = NtpBackendResult::Failure;
    TEST_ASSERT_TRUE(ntp.requestSync(true, 2000U));
    ntp.update(true, 2001U);

    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(output));
    TEST_ASSERT_EQUAL_UINT8(10U, output.hour);
}

void test_ntp_instances_are_isolated() {
    FakeRtcBus firstBus;
    FakeRtcBus secondBus;
    fillRegisters(firstBus, 2026U, 1U, 1U, 0U, 0U, 0U);
    fillRegisters(secondBus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService firstRtc(firstBus, rtcConfig());
    RtcService secondRtc(secondBus, rtcConfig());

    FakeNtpBackend firstBackend;
    FakeNtpBackend secondBackend;
    NtpService firstNtp(firstRtc, firstBackend);
    NtpService secondNtp(secondRtc, secondBackend);
    firstNtp.begin(0U);
    secondNtp.begin(0U);

    firstBackend.result = NtpBackendResult::Success;
    firstBackend.utc = utcAt(2026U, 9U, 11U, 10U, 0U, 0U);
    firstNtp.requestSync(true, 100U);
    firstNtp.update(true, 101U);

    UtcDateTime first {};
    UtcDateTime second {};
    TEST_ASSERT_TRUE(firstNtp.takeReceivedUtc(first));
    TEST_ASSERT_FALSE(secondNtp.takeReceivedUtc(second));
}

void test_ntp_begin_clears_pending_and_fetch_state() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    TEST_ASSERT_TRUE(rtc.begin());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 10U, 0U, 0U);
    NtpService ntp(rtc, backend);
    TEST_ASSERT_TRUE(ntp.begin());

    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);
    TEST_ASSERT_TRUE(ntp.lastFetchSucceeded());

    TEST_ASSERT_TRUE(ntp.begin(200U));
    TEST_ASSERT_FALSE(ntp.lastFetchSucceeded());
    UtcDateTime output {};
    TEST_ASSERT_FALSE(ntp.takeReceivedUtc(output));
}

void test_convert_utc_to_warsaw_invalid_utc() {
    LocalTime invalidUtc {};
    invalidUtc.valid = false;
    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(invalidUtc);
    TEST_ASSERT_FALSE(warsaw.valid);
}

void test_convert_utc_to_warsaw_winter_day() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 1U; utc.day = 15U;
    utc.hour = 12U; utc.minute = 0U; utc.second = 0U;
    utc.minuteOfDay = 12U * 60U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    assertDateTime(warsaw, 2026U, 1U, 15U, 13U, 0U, 0U);
}

void test_convert_utc_to_warsaw_summer_day() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 7U; utc.day = 15U;
    utc.hour = 12U; utc.minute = 0U; utc.second = 0U;
    utc.minuteOfDay = 12U * 60U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    assertDateTime(warsaw, 2026U, 7U, 15U, 14U, 0U, 0U);
}

void test_convert_utc_to_warsaw_midnight_winter() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 1U; utc.day = 15U;
    utc.hour = 23U; utc.minute = 30U; utc.second = 0U;
    utc.minuteOfDay = 23U * 60U + 30U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    assertDateTime(warsaw, 2026U, 1U, 16U, 0U, 30U, 0U);
}

void test_convert_utc_to_warsaw_midnight_summer() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 7U; utc.day = 15U;
    utc.hour = 22U; utc.minute = 45U; utc.second = 0U;
    utc.minuteOfDay = 22U * 60U + 45U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    assertDateTime(warsaw, 2026U, 7U, 16U, 0U, 45U, 0U);
}

void test_convert_utc_to_warsaw_year_boundary() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 12U; utc.day = 31U;
    utc.hour = 23U; utc.minute = 30U; utc.second = 0U;
    utc.minuteOfDay = 23U * 60U + 30U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    assertDateTime(warsaw, 2027U, 1U, 1U, 0U, 30U, 0U);
}

void test_convert_utc_to_warsaw_dst_march_transitions() {
    // 2024-03-31 00:59:00 UTC (przed zmiana: CET = UTC+1 -> 01:59:00)
    LocalTime beforeUtc {};
    beforeUtc.valid = true;
    beforeUtc.year = 2024U; beforeUtc.month = 3U; beforeUtc.day = 31U;
    beforeUtc.hour = 0U; beforeUtc.minute = 59U; beforeUtc.second = 0U;
    beforeUtc.minuteOfDay = 59U;

    LocalTime beforeWarsaw = EuropeWarsawTimeService::convertUtcToWarsaw(beforeUtc);
    assertDateTime(beforeWarsaw, 2024U, 3U, 31U, 1U, 59U, 0U);

    // 2024-03-31 01:00:00 UTC (po zmianie: CEST = UTC+2 -> 03:00:00)
    LocalTime afterUtc {};
    afterUtc.valid = true;
    afterUtc.year = 2024U; afterUtc.month = 3U; afterUtc.day = 31U;
    afterUtc.hour = 1U; afterUtc.minute = 0U; afterUtc.second = 0U;
    afterUtc.minuteOfDay = 60U;

    LocalTime afterWarsaw = EuropeWarsawTimeService::convertUtcToWarsaw(afterUtc);
    assertDateTime(afterWarsaw, 2024U, 3U, 31U, 3U, 0U, 0U);
}

void test_convert_utc_to_warsaw_dst_october_transitions() {
    // 2024-10-27 00:59:00 UTC (przed zmiana: CEST = UTC+2 -> 02:59:00)
    LocalTime beforeUtc {};
    beforeUtc.valid = true;
    beforeUtc.year = 2024U; beforeUtc.month = 10U; beforeUtc.day = 27U;
    beforeUtc.hour = 0U; beforeUtc.minute = 59U; beforeUtc.second = 0U;
    beforeUtc.minuteOfDay = 59U;

    LocalTime beforeWarsaw = EuropeWarsawTimeService::convertUtcToWarsaw(beforeUtc);
    assertDateTime(beforeWarsaw, 2024U, 10U, 27U, 2U, 59U, 0U);

    // 2024-10-27 01:00:00 UTC (po zmianie: CET = UTC+1 -> 02:00:00)
    LocalTime afterUtc {};
    afterUtc.valid = true;
    afterUtc.year = 2024U; afterUtc.month = 10U; afterUtc.day = 27U;
    afterUtc.hour = 1U; afterUtc.minute = 0U; afterUtc.second = 0U;
    afterUtc.minuteOfDay = 60U;

    LocalTime afterWarsaw = EuropeWarsawTimeService::convertUtcToWarsaw(afterUtc);
    assertDateTime(afterWarsaw, 2024U, 10U, 27U, 2U, 0U, 0U);
}

void test_convert_utc_to_warsaw_minute_of_day_correct() {
    LocalTime utc {};
    utc.valid = true;
    utc.year = 2026U; utc.month = 7U; utc.day = 15U;
    utc.hour = 8U; utc.minute = 45U; utc.second = 10U;
    utc.minuteOfDay = 8U * 60U + 45U;

    LocalTime warsaw = EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    TEST_ASSERT_EQUAL_UINT16(10U * 60U + 45U, warsaw.minuteOfDay);
}

void test_convert_utc_to_warsaw_matches_existing_service_now() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 7U, 15U, 10U, 30U, 0U);
    RtcService rtc(bus, rtcConfig());
    EuropeWarsawTimeService service(rtc);
    TEST_ASSERT_TRUE(service.begin());

    LocalTime fromService = service.now();
    LocalTime fromStatic = EuropeWarsawTimeService::convertUtcToWarsaw(rtc.read());

    assertDateTime(fromService, fromStatic.year, fromStatic.month, fromStatic.day,
                   fromStatic.hour, fromStatic.minute, fromStatic.second);
    TEST_ASSERT_EQUAL_UINT16(fromService.minuteOfDay, fromStatic.minuteOfDay);
}

void test_resilient_is_probe_due_when_healthy() {
    FakeRtcBus bus;
    fillRegisters(bus, 2026U, 9U, 11U, 10U, 0U, 0U);
    RtcService rtc(bus, rtcConfig());
    ResilientTimeService time(rtc);
    TEST_ASSERT_TRUE(time.begin(0U));

    TEST_ASSERT_TRUE(time.isRtcHealthy());
    TEST_ASSERT_TRUE(time.isProbeDue(100U));
    TEST_ASSERT_TRUE(time.isProbeDue(1000U));
}

void test_resilient_is_probe_due_throttled_when_unhealthy() {
    FakeRtcBus bus;
    bus.failNextRead = true;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.unhealthyProbeIntervalMs = 30000U;
    ResilientTimeService time(rtc, config);

    TEST_ASSERT_FALSE(time.begin(1000U));
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    // Przed uplywem 30s probe is NOT due
    TEST_ASSERT_FALSE(time.isProbeDue(15000U));
    TEST_ASSERT_FALSE(time.isProbeDue(30999U));

    // Po 30s od ostatniego poll (1000U + 30000U = 31000U) probe is DUE
    TEST_ASSERT_TRUE(time.isProbeDue(31000U));
    TEST_ASSERT_TRUE(time.isProbeDue(35000U));

    // Po wykonaniu poll timer przesuwa sie o kolejne 30s
    bus.failNextRead = true;
    time.poll(31000U);
    TEST_ASSERT_FALSE(time.isProbeDue(31500U));
    TEST_ASSERT_FALSE(time.isProbeDue(60999U));
    TEST_ASSERT_TRUE(time.isProbeDue(61000U));
}

void test_resilient_is_probe_due_handles_millis_rollover() {
    FakeRtcBus bus;
    bus.failNextRead = true;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.unhealthyProbeIntervalMs = 30000U;
    ResilientTimeService time(rtc, config);

    TEST_ASSERT_FALSE(time.begin(UINT32_MAX - 5000U));
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    TEST_ASSERT_FALSE(time.isProbeDue(UINT32_MAX - 1000U));
    TEST_ASSERT_FALSE(time.isProbeDue(24999U));
    TEST_ASSERT_TRUE(time.isProbeDue(25000U));
}

void test_resilient_ntp_only_operation_without_rtc() {
    FakeRtcBus bus;
    bus.failNextRead = true; // Brak RTC fizycznie
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.unhealthyProbeIntervalMs = 30000U;
    ResilientTimeService time(rtc, config);

    TEST_ASSERT_FALSE(time.begin(0U));
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_FALSE(time.isValid());

    // Odbieramy czas z NTP i synchronizujemy do ResilientTimeService
    LocalTime ntpUtc {};
    ntpUtc.valid = true;
    ntpUtc.year = 2026U; ntpUtc.month = 9U; ntpUtc.day = 11U;
    ntpUtc.hour = 14U; ntpUtc.minute = 30U; ntpUtc.second = 0U;
    ntpUtc.minuteOfDay = 14U * 60U + 30U;

    time.syncUtc(ntpUtc, 5000U);

    // Czas staje sie w pelni VALID, mimo ze RTC pozostaje UNHEALTHY
    TEST_ASSERT_TRUE(time.isValid());
    TEST_ASSERT_FALSE(time.isRtcHealthy());

    // Sprawdzamy monotoniczna interpolacje
    assertDateTime(time.now(5000U), 2026U, 9U, 11U, 14U, 30U, 0U);
    assertDateTime(time.now(15000U), 2026U, 9U, 11U, 14U, 30U, 10U);
    assertDateTime(time.now(65000U), 2026U, 9U, 11U, 14U, 31U, 0U);
}

void test_resilient_disabled_rtc_never_touches_hardware() {
    FakeRtcBus bus;
    RtcService rtc(bus, rtcConfig());
    ResilientTimeConfig config {};
    config.rtcEnabled = false; // RTC jawnie wylaczony w konfiguracji
    ResilientTimeService time(rtc, config);

    TEST_ASSERT_TRUE(time.begin(0U));
    TEST_ASSERT_FALSE(time.isRtcConfigured());
    TEST_ASSERT_FALSE(time.isRtcHealthy());
    TEST_ASSERT_FALSE(time.isValid());
    TEST_ASSERT_FALSE(time.isProbeDue(100000U));
    TEST_ASSERT_EQUAL_INT(0, bus.readCalls);
    TEST_ASSERT_EQUAL_INT(0, bus.writeCalls);

    // Poll nie wykonuje zadnych operacji I2C
    time.poll(1000U);
    time.poll(31000U);
    TEST_ASSERT_EQUAL_INT(0, bus.readCalls);

    // Po syncUtc staje sie w pelni VALID
    LocalTime ntpUtc {};
    ntpUtc.valid = true;
    ntpUtc.year = 2026U; ntpUtc.month = 9U; ntpUtc.day = 11U;
    ntpUtc.hour = 16U; ntpUtc.minute = 0U; ntpUtc.second = 0U;
    ntpUtc.minuteOfDay = 16U * 60U;

    time.syncUtc(ntpUtc, 35000U);
    TEST_ASSERT_TRUE(time.isValid());
    assertDateTime(time.now(36000U), 2026U, 9U, 11U, 16U, 0U, 1U);
}

void test_ntp_service_rtc_sync_disabled_skips_rtc_write() {
    FakeRtcBus bus;
    RtcService rtc(bus, rtcConfig());
    FakeNtpBackend backend;
    backend.result = NtpBackendResult::Success;
    backend.utc = utcAt(2026U, 9U, 11U, 12U, 0U, 0U);

    NtpService ntp(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.rtcSyncEnabled = false; // Wylaczony zapis do RTC

    TEST_ASSERT_TRUE(ntp.begin(config, 0U));
    TEST_ASSERT_TRUE(ntp.requestSync(true, 100U));
    ntp.update(true, 101U);

    TEST_ASSERT_TRUE(ntp.lastFetchSucceeded());
    TEST_ASSERT_FALSE(ntp.lastSyncSucceeded()); // Bo rtcUpdated == false
    TEST_ASSERT_EQUAL_INT(0, bus.writeCalls);   // ZERO zapisow do DS3231

    UtcDateTime output {};
    TEST_ASSERT_TRUE(ntp.takeReceivedUtc(output));
    TEST_ASSERT_EQUAL_UINT16(2026U, output.year);
    TEST_ASSERT_EQUAL_UINT8(12U, output.hour);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_valid_rtc_read);
    RUN_TEST(test_osf_at_boot_marks_rtc_invalid);
    RUN_TEST(test_osf_appearing_at_runtime_invalidates_rtc);
    RUN_TEST(test_invalid_bcd_is_rejected);
    RUN_TEST(test_invalid_date_is_rejected);
    RUN_TEST(test_leap_year_february_29_is_valid);
    RUN_TEST(test_non_leap_february_29_is_rejected);
    RUN_TEST(test_valid_set_utc_is_written_and_verified);
    RUN_TEST(test_set_utc_clears_osf);
    RUN_TEST(test_i2c_failure_returns_false);
    RUN_TEST(test_acknowledged_unapplied_write_is_rejected);
    RUN_TEST(test_ntp_request_starts_non_blocking);
    RUN_TEST(test_ntp_timeout_finishes_as_failure);
    RUN_TEST(test_ntp_success_writes_rtc);
    RUN_TEST(test_ntp_failure_does_not_alter_rtc);
    RUN_TEST(test_set_utc_failure_makes_sync_fail);
    RUN_TEST(test_parallel_sync_is_rejected);
    RUN_TEST(test_ntp_timeout_handles_millis_overflow);
    RUN_TEST(test_ntp_keeps_utc_without_timezone_offset);
    RUN_TEST(test_europe_warsaw_winter_conversion_is_unchanged);
    RUN_TEST(test_dst_start_boundary_is_unchanged);
    RUN_TEST(test_dst_end_boundary_is_unchanged);
    RUN_TEST(test_invalid_rtc_remains_invalid);
    RUN_TEST(test_ntp_recovers_invalid_rtc);
    RUN_TEST(test_resilient_boot_valid_rtc_is_healthy_and_valid);
    RUN_TEST(test_resilient_boot_failed_rtc_is_unhealthy_and_invalid);
    RUN_TEST(test_resilient_single_failure_maintains_healthy);
    RUN_TEST(test_resilient_two_failures_maintain_healthy);
    RUN_TEST(test_resilient_three_failures_drop_healthy);
    RUN_TEST(test_resilient_cache_remains_valid_when_rtc_unhealthy);
    RUN_TEST(test_resilient_interpolation_works_when_rtc_unhealthy);
    RUN_TEST(test_resilient_recovery_requires_exact_threshold_successes);
    RUN_TEST(test_resilient_failure_during_recovery_resets_success_counter);
    RUN_TEST(test_resilient_success_during_failure_sequence_resets_failure_counter);
    RUN_TEST(test_resilient_successful_poll_refreshes_cache);
    RUN_TEST(test_resilient_invalid_rtc_reading_treated_as_failure);
    RUN_TEST(test_resilient_interpolation_advances_65_seconds);
    RUN_TEST(test_resilient_handles_millis_rollover);
    RUN_TEST(test_resilient_sync_utc_updates_cache);
    RUN_TEST(test_resilient_sync_utc_does_not_mark_hardware_healthy);
    RUN_TEST(test_resilient_two_instances_are_isolated);
    RUN_TEST(test_resilient_counters_do_not_overflow);
    RUN_TEST(test_resilient_sync_utc_rejects_invalid_date_fields);
    RUN_TEST(test_resilient_normalizes_zero_thresholds_to_one);
    RUN_TEST(test_resilient_calendar_transitions);
    RUN_TEST(test_resilient_custom_failure_threshold);
    RUN_TEST(test_resilient_custom_recovery_threshold);
    RUN_TEST(test_ntp_fetch_success_and_rtc_write_success);
    RUN_TEST(test_ntp_fetch_success_even_when_rtc_write_fails);
    RUN_TEST(test_ntp_take_received_utc_consumes_result_exactly_once);
    RUN_TEST(test_ntp_invalid_utc_does_not_set_fetch_success);
    RUN_TEST(test_ntp_backend_failure_resets_fetch_success);
    RUN_TEST(test_ntp_timeout_resets_fetch_success);
    RUN_TEST(test_ntp_wifi_loss_resets_fetch_success);
    RUN_TEST(test_ntp_subsequent_sync_produces_new_consumable_utc);
    RUN_TEST(test_ntp_two_fetches_without_consumption_keeps_latest);
    RUN_TEST(test_ntp_pending_preserved_if_subsequent_attempt_fails);
    RUN_TEST(test_ntp_instances_are_isolated);
    RUN_TEST(test_ntp_begin_clears_pending_and_fetch_state);
    RUN_TEST(test_convert_utc_to_warsaw_invalid_utc);
    RUN_TEST(test_convert_utc_to_warsaw_winter_day);
    RUN_TEST(test_convert_utc_to_warsaw_summer_day);
    RUN_TEST(test_convert_utc_to_warsaw_midnight_winter);
    RUN_TEST(test_convert_utc_to_warsaw_midnight_summer);
    RUN_TEST(test_convert_utc_to_warsaw_year_boundary);
    RUN_TEST(test_convert_utc_to_warsaw_dst_march_transitions);
    RUN_TEST(test_convert_utc_to_warsaw_dst_october_transitions);
    RUN_TEST(test_convert_utc_to_warsaw_minute_of_day_correct);
    RUN_TEST(test_convert_utc_to_warsaw_matches_existing_service_now);
    RUN_TEST(test_resilient_is_probe_due_when_healthy);
    RUN_TEST(test_resilient_is_probe_due_throttled_when_unhealthy);
    RUN_TEST(test_resilient_is_probe_due_handles_millis_rollover);
    RUN_TEST(test_resilient_ntp_only_operation_without_rtc);
    RUN_TEST(test_resilient_disabled_rtc_never_touches_hardware);
    RUN_TEST(test_ntp_service_rtc_sync_disabled_skips_rtc_write);

    UNITY_END();
}

void loop() {
}
