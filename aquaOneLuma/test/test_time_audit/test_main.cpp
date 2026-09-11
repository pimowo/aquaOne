#include <Arduino.h>
#include <unity.h>

#include <climits>

#include "../test_stage13/Preferences.h"
#include "../test_stage13/Wire.h"

#define LUMASENSE_STORAGE_PREFERENCES_HEADER "../../test/test_stage13/Preferences.h"
#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_stage13/Wire.h"

TwoWire Wire;

#include "../../src/storage/ConfigDefaults.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/storage/StorageService.cpp"
#include "../../src/time/RtcService.cpp"
#include "../../src/time/TimeService.cpp"
#include "../../src/time/NtpSyncCoordinator.cpp"
#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/core/LumaCore.cpp"
#include "../../src/app/FirmwareApp.cpp"

using namespace LumaSense;

namespace {

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4U) | (value % 10U)
    );
}

void fillRtc(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    Wire.reg(0x00) = toBcd(second);
    Wire.reg(0x01) = toBcd(minute);
    Wire.reg(0x02) = toBcd(hour);
    Wire.reg(0x03) = toBcd(1U);
    Wire.reg(0x04) = toBcd(day);
    Wire.reg(0x05) = toBcd(month);
    Wire.reg(0x06) = toBcd(
        static_cast<uint8_t>(year % 100U)
    );
    Wire.reg(0x0F) = 0U;
}

void fillBaseRtc(uint8_t second = 0U) {
    fillRtc(2026U, 9U, 10U, 10U, 0U, second);
}

class MockHardware final : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override {
        ready_ = channels != nullptr;
        return ready_;
    }

    bool setChannelPercent(
        uint8_t channel,
        float
    ) override {
        return ready_ && channel < CHANNEL_COUNT;
    }

    bool allChannelsOff() override {
        return ready_;
    }

    bool isReady() const override {
        return ready_;
    }

    uint8_t availableChannelCount() const override {
        return CHANNEL_COUNT;
    }

    bool isChannelAvailable(uint8_t channel) const override {
        return channel < CHANNEL_COUNT;
    }

private:
    bool ready_ = false;
};

struct FirmwareFixture {
    MockHardware hardware;
    StorageService storage;
    TimeService timeService;
    FirmwareApp app;

    FirmwareFixture()
        : app(hardware, storage, timeService) {
    }

    void begin(uint32_t nowMs) {
        fillBaseRtc();
        TEST_ASSERT_TRUE(app.begin(nowMs));
        assertLocal(12U, 0U, 0U);
    }

    void assertLocal(
        uint8_t hour,
        uint8_t minute,
        uint8_t second
    ) const {
        const LocalTime& value = app.localTime();
        TEST_ASSERT_TRUE(value.valid);
        TEST_ASSERT_EQUAL_UINT8(hour, value.hour);
        TEST_ASSERT_EQUAL_UINT8(minute, value.minute);
        TEST_ASSERT_EQUAL_UINT8(second, value.second);
    }
};

class FakeNtpBackend final : public NtpBackend {
public:
    bool start(
        const char* const[NTP_MAX_SERVERS],
        uint8_t serverCount
    ) override {
        ++startCalls;
        return serverCount > 0U;
    }

    NtpBackendResult poll(UtcDateTime& output) override {
        ++pollCalls;
        output = utc;
        return result;
    }

    void stop() override {
        ++stopCalls;
    }

    NtpBackendResult result = NtpBackendResult::Success;
    UtcDateTime utc {};
    uint16_t startCalls = 0U;
    uint16_t pollCalls = 0U;
    uint16_t stopCalls = 0U;
};

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

void test_regular_1000ms_updates_do_not_lose_seconds() {
    FirmwareFixture fixture;
    fixture.begin(0U);

    for (uint8_t second = 1U; second <= 5U; ++second) {
        fillBaseRtc(second);
        TEST_ASSERT_TRUE(
            fixture.app.update(
                static_cast<uint32_t>(second) * 1000U
            )
        );
        fixture.assertLocal(12U, 0U, second);
    }
}

void test_regular_1100ms_updates_do_not_accumulate_lag() {
    FirmwareFixture fixture;
    fixture.begin(0U);

    for (uint8_t second = 1U; second <= 5U; ++second) {
        fillBaseRtc(second);
        TEST_ASSERT_TRUE(
            fixture.app.update(
                static_cast<uint32_t>(second) * 1100U
            )
        );
        fixture.assertLocal(12U, 0U, second);
    }
}

void test_five_second_pause_is_recovered_from_rtc() {
    FirmwareFixture fixture;
    fixture.begin(0U);

    fillBaseRtc(5U);
    TEST_ASSERT_TRUE(fixture.app.update(5000U));
    fixture.assertLocal(12U, 0U, 5U);
}

void test_twenty_second_pause_is_recovered_from_rtc() {
    FirmwareFixture fixture;
    fixture.begin(0U);

    fillBaseRtc(20U);
    TEST_ASSERT_TRUE(fixture.app.update(20000U));
    fixture.assertLocal(12U, 0U, 20U);
}

void test_rtc_scheduler_and_age_handle_millis_rollover() {
    constexpr uint32_t startMs = UINT32_MAX - 500U;
    FirmwareFixture fixture;
    fixture.begin(startMs);

    TEST_ASSERT_TRUE(fixture.app.update(UINT32_MAX - 50U));
    TEST_ASSERT_EQUAL_UINT32(
        450U,
        fixture.app.rtcReadAgeMs(UINT32_MAX - 50U)
    );
    fixture.assertLocal(12U, 0U, 0U);

    fillBaseRtc(1U);
    TEST_ASSERT_TRUE(fixture.app.update(499U));
    fixture.assertLocal(12U, 0U, 1U);
    TEST_ASSERT_EQUAL_UINT32(
        0U,
        fixture.app.rtcReadAgeMs(499U)
    );

    fillBaseRtc(21U);
    TEST_ASSERT_TRUE(fixture.app.update(20499U));
    fixture.assertLocal(12U, 0U, 21U);
}

void test_ntp_corrects_accumulated_rtc_lag_initially_and_every_24h() {
    fillBaseRtc(0U);
    TimeService timeService;
    TEST_ASSERT_TRUE(timeService.begin());

    FakeNtpBackend backend;
    NtpService ntp(timeService.rtcService(), backend);
    NtpSyncCoordinator coordinator(ntp);
    TEST_ASSERT_TRUE(coordinator.begin(0U));

    coordinator.update(false, 100U);
    TEST_ASSERT_EQUAL_UINT16(0U, backend.startCalls);

    backend.utc = utcAt(2026U, 9U, 10U, 10U, 0U, 30U);
    coordinator.update(true, 101U);

    TEST_ASSERT_EQUAL_UINT16(1U, backend.startCalls);
    TEST_ASSERT_TRUE(ntp.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_HEX8(toBcd(30U), Wire.reg(0x00));

    // The RTC is deliberately ten seconds slow before the next correction.
    fillRtc(2026U, 9U, 11U, 10U, 0U, 50U);
    backend.utc = utcAt(2026U, 9U, 11U, 10U, 1U, 0U);

    coordinator.update(
        true,
        NTP_SYNC_INTERVAL_MS + 101U
    );
    TEST_ASSERT_TRUE(ntp.isSyncInProgress());
    coordinator.update(
        true,
        NTP_SYNC_INTERVAL_MS + 102U
    );

    TEST_ASSERT_EQUAL_UINT16(2U, backend.startCalls);
    TEST_ASSERT_TRUE(ntp.lastSyncSucceeded());
    TEST_ASSERT_EQUAL_HEX8(toBcd(11U), Wire.reg(0x04));
    TEST_ASSERT_EQUAL_HEX8(toBcd(1U), Wire.reg(0x01));
    TEST_ASSERT_EQUAL_HEX8(toBcd(0U), Wire.reg(0x00));
}

} // namespace

void setUp() {
    Preferences::reset();
    Wire.reset();
}

void tearDown() {
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_regular_1000ms_updates_do_not_lose_seconds);
    RUN_TEST(test_regular_1100ms_updates_do_not_accumulate_lag);
    RUN_TEST(test_five_second_pause_is_recovered_from_rtc);
    RUN_TEST(test_twenty_second_pause_is_recovered_from_rtc);
    RUN_TEST(test_rtc_scheduler_and_age_handle_millis_rollover);
    RUN_TEST(test_ntp_corrects_accumulated_rtc_lag_initially_and_every_24h);

    UNITY_END();
}

void loop() {
}
