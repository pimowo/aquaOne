#include <Arduino.h>
#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "AquaCore/Time/EuropeWarsawTimeService.h"
#include "AquaCore/Time/NtpService.h"
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

    UNITY_END();
}

void loop() {
}