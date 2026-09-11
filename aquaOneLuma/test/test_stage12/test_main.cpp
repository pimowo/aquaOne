#include <Arduino.h>
#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "Wire.h"

#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_stage12/Wire.h"

TwoWire Wire;

#include "../../src/time/RtcService.cpp"
#include "../../src/time/NtpService.cpp"
#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/core/LumaCore.cpp"

using namespace LumaSense;

void setUp() {
    Wire.reset();
}

void tearDown() {
}

namespace {

constexpr uint32_t DAY_SECONDS = 24UL * 60UL * 60UL;

class FakeNtpBackend final : public NtpBackend {
public:
    bool start(
        const char* const servers[NTP_MAX_SERVERS],
        uint8_t serverCount
    ) override {
        ++startCalls;
        capturedServerCount = serverCount;

        for (uint8_t index = 0; index < NTP_MAX_SERVERS; ++index) {
            capturedServers[index][0] = '\0';

            if (
                index < serverCount &&
                servers[index] != nullptr
            ) {
                strncpy(
                    capturedServers[index],
                    servers[index],
                    NTP_SERVER_NAME_CAPACITY - 1U
                );
                capturedServers[index]
                    [NTP_SERVER_NAME_CAPACITY - 1U] = '\0';
            }
        }

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

    uint16_t startCalls = 0;
    uint16_t pollCalls = 0;
    uint16_t stopCalls = 0;
    uint8_t capturedServerCount = 0;
    char capturedServers[NTP_MAX_SERVERS]
        [NTP_SERVER_NAME_CAPACITY] {};
};

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4) | (value % 10U)
    );
}

void fillRegisters(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    uint8_t status = 0U
) {
    Wire.reg(0x00) = toBcd(second);
    Wire.reg(0x01) = toBcd(minute);
    Wire.reg(0x02) = toBcd(hour);
    Wire.reg(0x03) = toBcd(1U);
    Wire.reg(0x04) = toBcd(day);
    Wire.reg(0x05) = toBcd(month);

    if (year >= 2100U) {
        Wire.reg(0x05) |= 0x80U;
    }

    Wire.reg(0x06) =
        toBcd(static_cast<uint8_t>(year % 100U));
    Wire.reg(0x0F) = status;
}

UtcDateTime utcAt(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    UtcDateTime utc {};
    utc.year = year;
    utc.month = month;
    utc.day = day;
    utc.hour = hour;
    utc.minute = minute;
    utc.second = second;
    return utc;
}

LocalTime localAtSecond(uint32_t secondOfDay) {
    secondOfDay %= DAY_SECONDS;

    LocalTime time {};
    time.valid = true;
    time.year = 2026U;
    time.month = 9U;
    time.day = 8U;
    time.hour = static_cast<uint8_t>(secondOfDay / 3600UL);
    time.minute = static_cast<uint8_t>((secondOfDay / 60UL) % 60UL);
    time.second = static_cast<uint8_t>(secondOfDay % 60UL);
    time.minuteOfDay = static_cast<uint16_t>(secondOfDay / 60UL);
    return time;
}

uint32_t secondsAt(
    uint8_t hour,
    uint8_t minute,
    uint8_t second = 0U
) {
    return
        static_cast<uint32_t>(hour) * 3600UL +
        static_cast<uint32_t>(minute) * 60UL +
        second;
}

DeviceConfig makeConfig() {
    DeviceConfig config {};
    config.activeProfileIndex = 0U;
    config.serviceProfileIndex = 1U;
    config.globalPowerLimitPercent = 100.0f;

    const float stages[DAY_STAGE_COUNT] = {
        10.0f, 25.0f, 40.0f, 80.0f,
        60.0f, 45.0f, 25.0f, 10.0f
    };

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        config.channels[channel].enabled = true;
        config.channels[channel].hardMaxPercent = 100.0f;
        config.channels[channel].calibrationMinPercent = 0.0f;
        config.channels[channel].calibrationMaxPercent = 100.0f;
        config.channels[channel].gamma = 1.0f;
    }

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        Profile& current = config.profiles[profile];
        current.dayStartMinute = 480U;
        current.dayEndMinute = 1200U;
        current.nightEnabled = true;
        current.nightLevels.value[0] = 2.0f;

        for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
            current.stages[stage].levels.value[0] = stages[stage];
        }
    }

    return config;
}

void prepareRtc(RtcService& rtc) {
    fillRegisters(2026U, 9U, 8U, 10U, 0U, 0U);
    TEST_ASSERT_TRUE(rtc.begin());
}

void completeSync(
    NtpService& service,
    FakeNtpBackend& backend,
    const UtcDateTime& utc,
    uint32_t requestedMs,
    uint32_t completedMs
) {
    backend.utc = utc;
    backend.result = NtpBackendResult::Success;
    TEST_ASSERT_TRUE(service.requestSync(true, requestedMs));
    service.update(true, completedMs);
}

void settleCore(
    LumaCore& core,
    DeviceConfig& config,
    uint32_t firstSecond
) {
    TEST_ASSERT_TRUE(core.begin(config, 0U));
    core.update(localAtSecond(firstSecond), 0U);
    core.update(
        localAtSecond(firstSecond + 60U),
        STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_rtc_operates_without_ntp() {
    RtcService rtc;
    prepareRtc(rtc);

    const LocalTime value = rtc.read();
    TEST_ASSERT_TRUE(value.valid);
    TEST_ASSERT_EQUAL_UINT8(10U, value.hour);
    TEST_ASSERT_EQUAL_UINT8(0U, value.minute);
}

void test_request_sync_starts_without_polling_or_blocking() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);

    TEST_ASSERT_TRUE(service.begin(100U));
    TEST_ASSERT_TRUE(service.requestSync(true, 200U));
    TEST_ASSERT_TRUE(service.isSyncInProgress());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, backend.pollCalls);
}

void test_no_wifi_fails_without_changing_rtc() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    const int writesBefore = Wire.endTransmissionCalls();
    TEST_ASSERT_FALSE(service.requestSync(false, 10U));
    TEST_ASSERT_FALSE(service.isSyncInProgress());
    TEST_ASSERT_TRUE(service.hasSyncResult());
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_INT(writesBefore, Wire.endTransmissionCalls());
    TEST_ASSERT_EQUAL_UINT16(0U, backend.startCalls);
}

void test_unavailable_backend_fails_without_changing_rtc() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    backend.startResult = false;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    const int writesBefore = Wire.endTransmissionCalls();
    TEST_ASSERT_FALSE(service.requestSync(true, 10U));
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_INT(writesBefore, Wire.endTransmissionCalls());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.stopCalls);
}

void test_timeout_finishes_attempt_as_failure() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.timeoutMs = 100U;
    TEST_ASSERT_TRUE(service.begin(config, 0U));
    TEST_ASSERT_TRUE(service.requestSync(true, 500U));

    service.update(true, 599U);
    TEST_ASSERT_TRUE(service.isSyncInProgress());
    service.update(true, 600U);
    TEST_ASSERT_FALSE(service.isSyncInProgress());
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.stopCalls);
}

void test_valid_ntp_utc_is_written_to_rtc() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    completeSync(
        service,
        backend,
        utcAt(2028U, 2U, 29U, 21U, 45U, 37U),
        100U,
        101U
    );

    TEST_ASSERT_TRUE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_HEX8(0x37U, Wire.reg(0x00));
    TEST_ASSERT_EQUAL_HEX8(0x45U, Wire.reg(0x01));
    TEST_ASSERT_EQUAL_HEX8(0x21U, Wire.reg(0x02));
    TEST_ASSERT_EQUAL_HEX8(0x29U, Wire.reg(0x04));
    TEST_ASSERT_EQUAL_HEX8(0x02U, Wire.reg(0x05));
    TEST_ASSERT_EQUAL_HEX8(0x28U, Wire.reg(0x06));
}

void test_invalid_ntp_utc_is_rejected_before_rtc_write() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    backend.utc = utcAt(2026U, 2U, 30U, 12U, 0U, 0U);
    backend.result = NtpBackendResult::Success;
    const int transmissionsBefore = Wire.endTransmissionCalls();

    TEST_ASSERT_TRUE(service.requestSync(true, 100U));
    service.update(true, 101U);

    TEST_ASSERT_TRUE(service.hasSyncResult());
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_INT(
        transmissionsBefore,
        Wire.endTransmissionCalls()
    );
}
void test_sync_succeeds_only_after_verified_rtc_write() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 12U, 34U, 56U),
        100U,
        101U
    );

    TEST_ASSERT_TRUE(service.hasSyncResult());
    TEST_ASSERT_TRUE(service.lastSyncSucceeded());
    TEST_ASSERT_TRUE(rtc.isValid());
    TEST_ASSERT_FALSE(service.isSyncInProgress());
    TEST_ASSERT_EQUAL_UINT16(1U, backend.stopCalls);
}

void test_rtc_set_error_makes_sync_fail() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    backend.utc = utcAt(2026U, 9U, 8U, 12U, 34U, 56U);
    backend.result = NtpBackendResult::Success;
    TEST_ASSERT_TRUE(service.requestSync(true, 100U));
    Wire.failEndTransmissionOnCall(
        Wire.endTransmissionCalls() + 1
    );
    service.update(true, 101U);

    TEST_ASSERT_TRUE(service.hasSyncResult());
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_ntp_does_not_apply_dst_and_writes_utc() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    completeSync(
        service,
        backend,
        utcAt(2026U, 7U, 1U, 12U, 15U, 30U),
        100U,
        101U
    );

    TEST_ASSERT_TRUE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_HEX8(0x12U, Wire.reg(0x02));
    TEST_ASSERT_EQUAL_HEX8(0x15U, Wire.reg(0x01));
}

void test_sync_recovers_invalid_rtc() {
    fillRegisters(2026U, 9U, 8U, 10U, 0U, 0U, 0x80U);
    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());

    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());
    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 11U, 22U, 33U),
        100U,
        101U
    );

    TEST_ASSERT_TRUE(service.lastSyncSucceeded());
    TEST_ASSERT_TRUE(rtc.isValid());
    const LocalTime recovered = rtc.read();
    TEST_ASSERT_TRUE(recovered.valid);
    TEST_ASSERT_EQUAL_UINT8(11U, recovered.hour);
    TEST_ASSERT_EQUAL_UINT8(22U, recovered.minute);
}

void test_positive_correction_is_seen_by_core_as_time_jump() {
    RtcService rtc;
    prepareRtc(rtc);
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleCore(core, config, secondsAt(10U, 0U));
    const float before = core.state().actualLevels.value[0];

    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());
    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 10U, 11U, 0U),
        STANDARD_TRANSITION_MS + 100U,
        STANDARD_TRANSITION_MS + 101U
    );

    core.update(rtc.read(), STANDARD_TRANSITION_MS + 1000U);
    TEST_ASSERT_TRUE(core.state().transitionActive);
    TEST_ASSERT_FLOAT_WITHIN(
        0.003f,
        before,
        core.state().actualLevels.value[0]
    );
}

void test_negative_correction_is_seen_by_core_as_time_jump() {
    RtcService rtc;
    prepareRtc(rtc);
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleCore(core, config, secondsAt(10U, 30U));
    const float before = core.state().actualLevels.value[0];

    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());
    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 10U, 21U, 0U),
        STANDARD_TRANSITION_MS + 100U,
        STANDARD_TRANSITION_MS + 101U
    );

    core.update(rtc.read(), STANDARD_TRANSITION_MS + 1000U);
    TEST_ASSERT_TRUE(core.state().transitionActive);
    TEST_ASSERT_FLOAT_WITHIN(
        0.003f,
        before,
        core.state().actualLevels.value[0]
    );
}

void test_failed_sync_is_not_retried_by_update() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    backend.utc = utcAt(2026U, 9U, 8U, 12U, 0U, 0U);
    backend.result = NtpBackendResult::Success;
    TEST_ASSERT_TRUE(service.requestSync(true, 100U));
    Wire.failEndTransmissionOnCall(
        Wire.endTransmissionCalls() + 1
    );
    service.update(true, 101U);

    const int transmissionsAfterFailure =
        Wire.endTransmissionCalls();
    service.update(true, 102U);
    service.update(true, 5000U);

    TEST_ASSERT_EQUAL_INT(
        transmissionsAfterFailure,
        Wire.endTransmissionCalls()
    );
    TEST_ASSERT_EQUAL_UINT16(1U, backend.pollCalls);
}

void test_duplicate_requests_do_not_start_parallel_attempts() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    TEST_ASSERT_TRUE(service.requestSync(true, 100U));
    TEST_ASSERT_FALSE(service.requestSync(true, 101U));
    TEST_ASSERT_FALSE(service.requestPeriodicSync(true, 102U));
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
    TEST_ASSERT_TRUE(service.isSyncInProgress());
}

void test_timeout_handles_millis_overflow() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.timeoutMs = 1000U;
    TEST_ASSERT_TRUE(service.begin(config, UINT32_MAX - 1000U));
    TEST_ASSERT_TRUE(
        service.requestSync(true, UINT32_MAX - 500U)
    );

    service.update(true, 498U);
    TEST_ASSERT_TRUE(service.isSyncInProgress());
    service.update(true, 499U);
    TEST_ASSERT_FALSE(service.isSyncInProgress());
    TEST_ASSERT_FALSE(service.lastSyncSucceeded());
}

void test_completed_request_allows_next_sync() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 10U, 1U, 0U),
        100U,
        101U
    );
    backend.utc = utcAt(2026U, 9U, 8U, 10U, 2U, 0U);
    TEST_ASSERT_TRUE(service.requestSync(true, 200U));
    service.update(true, 201U);

    TEST_ASSERT_TRUE(service.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_UINT16(2U, backend.startCalls);
    TEST_ASSERT_EQUAL_UINT16(2U, backend.stopCalls);
    TEST_ASSERT_EQUAL_HEX8(0x02U, Wire.reg(0x01));
}

void test_last_sync_status_and_age_are_overflow_safe() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    TEST_ASSERT_TRUE(service.begin());

    uint32_t ageMs = 123U;
    TEST_ASSERT_FALSE(
        service.lastSuccessfulSyncAgeMs(100U, ageMs)
    );
    TEST_ASSERT_EQUAL_UINT32(0U, ageMs);

    completeSync(
        service,
        backend,
        utcAt(2026U, 9U, 8U, 10U, 1U, 0U),
        UINT32_MAX - 200U,
        UINT32_MAX - 100U
    );

    TEST_ASSERT_TRUE(
        service.lastSuccessfulSyncAgeMs(99U, ageMs)
    );
    TEST_ASSERT_EQUAL_UINT32(200U, ageMs);
}

void test_periodic_sync_waits_for_interval_and_does_not_poll_continuously() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);
    NtpConfig config = NtpService::defaultConfig();
    config.syncIntervalMs = 1000U;
    TEST_ASSERT_TRUE(service.begin(config, 100U));

    TEST_ASSERT_FALSE(service.isPeriodicSyncDue(1099U));
    TEST_ASSERT_FALSE(service.requestPeriodicSync(true, 1099U));
    TEST_ASSERT_EQUAL_UINT16(0U, backend.startCalls);

    TEST_ASSERT_TRUE(service.isPeriodicSyncDue(1100U));
    TEST_ASSERT_TRUE(service.requestPeriodicSync(true, 1100U));
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
    TEST_ASSERT_FALSE(service.requestPeriodicSync(true, 2100U));
    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
}

void test_configuration_accepts_one_to_three_servers() {
    RtcService rtc;
    prepareRtc(rtc);
    FakeNtpBackend backend;
    NtpService service(rtc, backend);

    NtpConfig one {};
    one.servers[0] = "one.example";
    one.serverCount = 1U;
    one.timeoutMs = 1000U;
    one.syncIntervalMs = 2000U;
    TEST_ASSERT_TRUE(service.begin(one));
    TEST_ASSERT_TRUE(service.requestSync(true, 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, backend.capturedServerCount);
    TEST_ASSERT_EQUAL_STRING(
        "one.example",
        backend.capturedServers[0]
    );

    backend.result = NtpBackendResult::Failure;
    service.update(true, 2U);

    NtpConfig three = NtpService::defaultConfig();
    TEST_ASSERT_TRUE(service.begin(three));
    TEST_ASSERT_TRUE(service.requestSync(true, 3U));
    TEST_ASSERT_EQUAL_UINT8(3U, backend.capturedServerCount);
    TEST_ASSERT_EQUAL_STRING(
        "time.cloudflare.com",
        backend.capturedServers[2]
    );

    NtpConfig invalid = three;
    invalid.serverCount = 0U;
    TEST_ASSERT_FALSE(service.begin(invalid));
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_rtc_operates_without_ntp);
    RUN_TEST(test_request_sync_starts_without_polling_or_blocking);
    RUN_TEST(test_no_wifi_fails_without_changing_rtc);
    RUN_TEST(test_unavailable_backend_fails_without_changing_rtc);
    RUN_TEST(test_timeout_finishes_attempt_as_failure);
    RUN_TEST(test_valid_ntp_utc_is_written_to_rtc);
    RUN_TEST(test_invalid_ntp_utc_is_rejected_before_rtc_write);
    RUN_TEST(test_sync_succeeds_only_after_verified_rtc_write);
    RUN_TEST(test_rtc_set_error_makes_sync_fail);
    RUN_TEST(test_ntp_does_not_apply_dst_and_writes_utc);
    RUN_TEST(test_sync_recovers_invalid_rtc);
    RUN_TEST(test_positive_correction_is_seen_by_core_as_time_jump);
    RUN_TEST(test_negative_correction_is_seen_by_core_as_time_jump);
    RUN_TEST(test_failed_sync_is_not_retried_by_update);
    RUN_TEST(test_duplicate_requests_do_not_start_parallel_attempts);
    RUN_TEST(test_timeout_handles_millis_overflow);
    RUN_TEST(test_completed_request_allows_next_sync);
    RUN_TEST(test_last_sync_status_and_age_are_overflow_safe);
    RUN_TEST(test_periodic_sync_waits_for_interval_and_does_not_poll_continuously);
    RUN_TEST(test_configuration_accepts_one_to_three_servers);

    UNITY_END();
}

void loop() {
}