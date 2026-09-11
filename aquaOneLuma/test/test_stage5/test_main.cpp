#include <Arduino.h>
#include <unity.h>

#include "Wire.h"

#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_stage5/Wire.h"

TwoWire Wire;

#include "../../src/time/RtcService.cpp"
#include "../../src/time/TimeService.cpp"
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

void copyDateTimeRegisters(uint8_t data[7]) {
    for (uint8_t index = 0; index < 7U; ++index) {
        data[index] = Wire.reg(index);
    }
}

void assertInvalidRegisters(uint8_t index, uint8_t value) {
    fillRegisters(2024U, 5U, 17U, 12U, 34U, 56U);

    uint8_t data[7] {};
    copyDateTimeRegisters(data);
    data[index] = value;

    LocalTime decoded {};
    TEST_ASSERT_FALSE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
    TEST_ASSERT_FALSE(decoded.valid);
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
    fillRegisters(2024U, 2U, 29U, 23U, 58U, 59U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());

    const LocalTime value = rtc.read();
    assertDateTime(value, 2024U, 2U, 29U, 23U, 58U, 59U);
    TEST_ASSERT_TRUE(rtc.isValid());
}

void test_osf_set_during_begin_marks_rtc_invalid() {
    fillRegisters(2024U, 5U, 17U, 12U, 0U, 0U, 0x80U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());
    TEST_ASSERT_FALSE(rtc.read().valid);
}

void test_osf_appearing_during_runtime_invalidates_rtc() {
    fillRegisters(2024U, 5U, 17U, 12U, 0U, 0U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_TRUE(rtc.read().valid);
    TEST_ASSERT_TRUE(rtc.isValid());

    Wire.reg(0x0F) |= 0x80U;

    TEST_ASSERT_FALSE(rtc.read().valid);
    TEST_ASSERT_FALSE(rtc.isValid());
}

void test_invalid_bcd_is_rejected() {
    assertInvalidRegisters(0U, 0x1AU);
}

void test_second_over_59_is_rejected() {
    assertInvalidRegisters(0U, 0x60U);
}

void test_minute_over_59_is_rejected() {
    assertInvalidRegisters(1U, 0x60U);
}

void test_hour_over_23_is_rejected() {
    assertInvalidRegisters(2U, 0x24U);
}

void test_day_zero_is_rejected() {
    assertInvalidRegisters(4U, 0x00U);
}

void test_day_beyond_month_length_is_rejected() {
    fillRegisters(2024U, 4U, 30U, 12U, 0U, 0U);

    uint8_t data[7] {};
    copyDateTimeRegisters(data);
    data[4] = 0x31U;

    LocalTime decoded {};
    TEST_ASSERT_FALSE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
}

void test_february_29_in_leap_year_is_valid() {
    fillRegisters(2024U, 2U, 29U, 12U, 0U, 0U);

    uint8_t data[7] {};
    copyDateTimeRegisters(data);

    LocalTime decoded {};
    TEST_ASSERT_TRUE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
    assertDateTime(decoded, 2024U, 2U, 29U, 12U, 0U, 0U);
}

void test_february_29_in_non_leap_year_is_rejected() {
    fillRegisters(2023U, 2U, 29U, 12U, 0U, 0U);

    uint8_t data[7] {};
    copyDateTimeRegisters(data);

    LocalTime decoded {};
    TEST_ASSERT_FALSE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
}

void test_invalid_month_is_rejected() {
    assertInvalidRegisters(5U, 0x13U);
}

void test_invalid_year_bcd_is_rejected() {
    assertInvalidRegisters(6U, 0x2AU);
}

void test_valid_12_hour_register_is_decoded() {
    fillRegisters(2024U, 5U, 17U, 0U, 15U, 0U);

    uint8_t data[7] {};
    copyDateTimeRegisters(data);
    data[2] = 0x72U;

    LocalTime decoded {};
    TEST_ASSERT_TRUE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
    TEST_ASSERT_EQUAL_UINT8(12U, decoded.hour);

    data[2] = 0x52U;
    TEST_ASSERT_TRUE(
        RtcService::decodeDateTimeRegisters(data, decoded)
    );
    TEST_ASSERT_EQUAL_UINT8(0U, decoded.hour);
}

void test_set_utc_writes_and_verifies_time() {
    fillRegisters(2024U, 1U, 1U, 0U, 0U, 0U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_TRUE(
        rtc.setUtc(2028U, 2U, 29U, 21U, 45U, 37U)
    );

    TEST_ASSERT_EQUAL_HEX8(0x37U, Wire.reg(0x00));
    TEST_ASSERT_EQUAL_HEX8(0x45U, Wire.reg(0x01));
    TEST_ASSERT_EQUAL_HEX8(0x21U, Wire.reg(0x02));
    TEST_ASSERT_EQUAL_HEX8(0x29U, Wire.reg(0x04));
    TEST_ASSERT_EQUAL_HEX8(0x02U, Wire.reg(0x05));
    TEST_ASSERT_EQUAL_HEX8(0x28U, Wire.reg(0x06));
    assertDateTime(
        rtc.read(),
        2028U,
        2U,
        29U,
        21U,
        45U,
        37U
    );
}

void test_set_utc_clears_osf() {
    fillRegisters(2024U, 1U, 1U, 0U, 0U, 0U, 0x8BU);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_FALSE(rtc.isValid());

    TEST_ASSERT_TRUE(
        rtc.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_EQUAL_HEX8(0x0BU, Wire.reg(0x0F));
    TEST_ASSERT_TRUE(rtc.isValid());
}

void test_i2c_errors_return_false_and_invalidate_state() {
    RtcService beginFailure;
    Wire.setBeginResult(false);
    TEST_ASSERT_FALSE(beginFailure.begin());
    TEST_ASSERT_FALSE(beginFailure.isValid());

    Wire.reset();
    Wire.failNextRequest();
    RtcService requestFailure;
    TEST_ASSERT_FALSE(requestFailure.begin());
    TEST_ASSERT_FALSE(requestFailure.isValid());

    Wire.reset();
    fillRegisters(2024U, 5U, 17U, 12U, 0U, 0U);
    RtcService writeFailure;
    TEST_ASSERT_TRUE(writeFailure.begin());
    Wire.failEndTransmissionOnCall(
        Wire.endTransmissionCalls() + 1
    );
    TEST_ASSERT_FALSE(
        writeFailure.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_FALSE(writeFailure.isValid());
}

void test_set_utc_fails_when_osf_clear_cannot_be_written() {
    fillRegisters(2024U, 1U, 1U, 0U, 0U, 0U, 0x80U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());

    Wire.failEndTransmissionOnCall(
        Wire.endTransmissionCalls() + 3
    );

    TEST_ASSERT_FALSE(
        rtc.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_FALSE(rtc.isValid());
    TEST_ASSERT_BITS_HIGH(0x80U, Wire.reg(0x0F));
}

void test_set_utc_fails_when_acknowledged_write_is_not_applied() {
    fillRegisters(2024U, 1U, 1U, 0U, 0U, 0U);

    RtcService rtc;
    TEST_ASSERT_TRUE(rtc.begin());

    Wire.setDiscardWrites(true);

    TEST_ASSERT_FALSE(
        rtc.setUtc(2026U, 9U, 8U, 10U, 20U, 30U)
    );
    TEST_ASSERT_FALSE(rtc.isValid());
}
void test_time_service_dst_boundaries_are_unchanged() {
    fillRegisters(2024U, 3U, 31U, 0U, 59U, 0U);

    TimeService service;
    TEST_ASSERT_TRUE(service.begin());

    LocalTime local = service.now();
    assertDateTime(local, 2024U, 3U, 31U, 1U, 59U, 0U);

    fillRegisters(2024U, 3U, 31U, 1U, 0U, 0U);
    local = service.now();
    assertDateTime(local, 2024U, 3U, 31U, 3U, 0U, 0U);

    fillRegisters(2024U, 10U, 27U, 0U, 59U, 0U);
    local = service.now();
    assertDateTime(local, 2024U, 10U, 27U, 2U, 59U, 0U);

    fillRegisters(2024U, 10U, 27U, 1U, 0U, 0U);
    local = service.now();
    assertDateTime(local, 2024U, 10U, 27U, 2U, 0U, 0U);
}

void test_invalid_time_keeps_lumacore_outputs_off() {
    DeviceConfig config {};
    config.globalPowerLimitPercent = 100.0f;
    config.profiles[0].dayStartMinute = 480U;
    config.profiles[0].dayEndMinute = 1140U;

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        config.profiles[0].stages[stage].levels.value[0] = 50.0f;
    }

    LumaCore core;
    LocalTime valid {};
    valid.valid = true;
    valid.hour = 12U;
    valid.minute = 0U;
    valid.second = 0U;
    valid.minuteOfDay = 720U;

    core.begin(config, 0U);
    core.update(valid, 0U);
    core.update(valid, STANDARD_TRANSITION_MS);
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        50.0f,
        core.state().actualLevels.value[0]
    );

    core.update(LocalTime {}, STANDARD_TRANSITION_MS + 1U);

    TEST_ASSERT_FALSE(core.state().timeValid);
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        0.0f,
        core.state().requestedLevels.value[0]
    );
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        0.0f,
        core.state().actualLevels.value[0]
    );
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_valid_rtc_read);
    RUN_TEST(test_osf_set_during_begin_marks_rtc_invalid);
    RUN_TEST(test_osf_appearing_during_runtime_invalidates_rtc);
    RUN_TEST(test_invalid_bcd_is_rejected);
    RUN_TEST(test_second_over_59_is_rejected);
    RUN_TEST(test_minute_over_59_is_rejected);
    RUN_TEST(test_hour_over_23_is_rejected);
    RUN_TEST(test_day_zero_is_rejected);
    RUN_TEST(test_day_beyond_month_length_is_rejected);
    RUN_TEST(test_february_29_in_leap_year_is_valid);
    RUN_TEST(test_february_29_in_non_leap_year_is_rejected);
    RUN_TEST(test_invalid_month_is_rejected);
    RUN_TEST(test_invalid_year_bcd_is_rejected);
    RUN_TEST(test_valid_12_hour_register_is_decoded);
    RUN_TEST(test_set_utc_writes_and_verifies_time);
    RUN_TEST(test_set_utc_clears_osf);
    RUN_TEST(test_i2c_errors_return_false_and_invalidate_state);
    RUN_TEST(test_set_utc_fails_when_osf_clear_cannot_be_written);
    RUN_TEST(test_set_utc_fails_when_acknowledged_write_is_not_applied);
    RUN_TEST(test_time_service_dst_boundaries_are_unchanged);
    RUN_TEST(test_invalid_time_keeps_lumacore_outputs_off);

    UNITY_END();
}

void loop() {
}