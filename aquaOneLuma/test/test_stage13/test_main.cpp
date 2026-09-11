#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "Preferences.h"
#include "Wire.h"

#define LUMASENSE_STORAGE_PREFERENCES_HEADER "../../test/test_stage13/Preferences.h"
#define LUMASENSE_RTC_WIRE_HEADER "../../test/test_stage13/Wire.h"

TwoWire Wire;

#include "../../src/storage/ConfigDefaults.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/storage/StorageService.cpp"
#include "../../src/time/RtcService.cpp"
#include "../../src/time/TimeService.cpp"
#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/core/LumaCore.cpp"
#include "../../src/app/FirmwareApp.cpp"

using namespace LumaSense;

namespace {

DeviceConfig configA;
DeviceConfig configB;

class MockHardware final : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override {
        ++beginCalls;
        ready = beginResult && channels != nullptr;

        if (channels != nullptr) {
            for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
                capturedInversion[channel] =
                    channels[channel].pwmInverted;
            }
        }

        return ready;
    }

    bool setChannelPercent(
        uint8_t channel,
        float percent
    ) override {
        ++setCalls;

        if (
            !ready ||
            channel >= CHANNEL_COUNT ||
            channel == failWriteChannel
        ) {
            ready = false;
            return false;
        }

        lastPercent[channel] = percent;
        return true;
    }

    bool allChannelsOff() override {
        ++allOffCalls;

        for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
            lastPercent[channel] = 0.0f;
        }

        return allOffResult;
    }

    bool isReady() const override {
        return ready;
    }

    uint8_t availableChannelCount() const override {
        return CHANNEL_COUNT;
    }

    bool isChannelAvailable(uint8_t channel) const override {
        return channel < CHANNEL_COUNT;
    }

    void invalidateRuntime() {
        ready = false;
    }

    bool beginResult = true;
    bool allOffResult = true;
    bool ready = false;
    uint8_t failWriteChannel = 0xFFU;

    uint16_t beginCalls = 0;
    uint32_t setCalls = 0;
    uint16_t allOffCalls = 0;

    bool capturedInversion[CHANNEL_COUNT] {};
    float lastPercent[CHANNEL_COUNT] {};
};

struct Fixture {
    MockHardware hardware;
    StorageService storage;
    TimeService timeService;
    FirmwareApp* app = nullptr;

    Fixture() {
        app = new FirmwareApp(
            hardware,
            storage,
            timeService
        );
    }

    ~Fixture() {
        delete app;
    }
};

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4) | (value % 10U)
    );
}

void fillRtc(
    uint16_t year = 2026U,
    uint8_t month = 9U,
    uint8_t day = 8U,
    uint8_t hour = 10U,
    uint8_t minute = 0U,
    uint8_t second = 0U,
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

void makeNvsConfig(DeviceConfig& config) {
    config = createDefaultConfig();
    config.activeProfileIndex = 2U;
    config.serviceProfileIndex = 1U;
    config.globalPowerLimitPercent = 90.0f;
    config.channels[0].pwmInverted = true;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        config.profiles[profile].dayStartMinute = 480U;
        config.profiles[profile].dayEndMinute = 1200U;
        config.profiles[profile].nightEnabled = true;

        for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
            config.profiles[profile].nightLevels.value[channel] = 5.0f;

            for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
                config.profiles[profile]
                    .stages[stage]
                    .levels.value[channel] =
                        40.0f + profile;
            }
        }
    }
}

void saveConfigToMockNvs(const DeviceConfig& config) {
    StorageService writer;
    TEST_ASSERT_TRUE(writer.begin());
    TEST_ASSERT_TRUE(writer.save(config));
}

void assertNormal(const FirmwareApp& app) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperatingMode::Normal),
        static_cast<uint8_t>(app.state().mode)
    );
}

} // namespace

void setUp() {
    Preferences::reset();
    Wire.reset();
    fillRtc();
    configA = {};
    configB = {};
}

void tearDown() {
}

namespace {

void test_hardware_rtc_and_nvs_config_start_in_normal() {
    makeNvsConfig(configA);
    saveConfigToMockNvs(configA);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->status().hardwareOk);
    TEST_ASSERT_TRUE(fixture.app->status().rtcOk);
    TEST_ASSERT_TRUE(fixture.app->status().storageOk);
    TEST_ASSERT_TRUE(fixture.app->status().coreOk);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Nvs),
        static_cast<uint8_t>(fixture.app->status().configSource)
    );
    assertNormal(*fixture.app);
}

void test_empty_storage_uses_defaults() {
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Defaults),
        static_cast<uint8_t>(fixture.app->status().configSource)
    );
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.app->config().activeProfileIndex);
    TEST_ASSERT_TRUE(ConfigValidator::validate(fixture.app->config()));
}

void test_storage_begin_failure_uses_defaults() {
    Preferences::setBeginResult(false);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_FALSE(fixture.app->status().storageOk);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Defaults),
        static_cast<uint8_t>(fixture.app->status().configSource)
    );
    TEST_ASSERT_TRUE(fixture.app->isRunning());
}

void test_invalid_storage_record_uses_defaults() {
    makeNvsConfig(configA);
    saveConfigToMockNvs(configA);
    Preferences::raw("cfg_a")[0] ^= 0x5AU;
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->status().storageOk);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Defaults),
        static_cast<uint8_t>(fixture.app->status().configSource)
    );
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.app->config().activeProfileIndex);
}

void test_factory_defaults_pass_validator() {
    configA = createDefaultConfig();
    TEST_ASSERT_TRUE(ConfigValidator::validate(configA));
}

void test_invalid_rtc_keeps_outputs_off() {
    fillRtc(2026U, 9U, 8U, 10U, 0U, 0U, 0x80U);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->status().rtcOk);
    TEST_ASSERT_FALSE(fixture.app->status().timeValid);
    TEST_ASSERT_TRUE(fixture.app->update(0U));
    TEST_ASSERT_FALSE(fixture.app->state().timeValid);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, fixture.hardware.allOffCalls);

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        TEST_ASSERT_FLOAT_WITHIN(
            0.0001f,
            0.0f,
            fixture.hardware.lastPercent[channel]
        );
    }
}

void test_rtc_recovery_starts_sixty_second_transition_from_zero() {
    makeNvsConfig(configA);
    saveConfigToMockNvs(configA);
    fillRtc(2026U, 9U, 8U, 10U, 0U, 0U, 0x80U);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->update(0U));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        0.0f,
        fixture.app->state().actualLevels.value[0]
    );

    fillRtc(2026U, 9U, 8U, 10U, 0U, 0U, 0U);
    TEST_ASSERT_TRUE(fixture.app->update(1000U));
    TEST_ASSERT_TRUE(fixture.app->state().timeValid);
    TEST_ASSERT_TRUE(fixture.app->state().transitionActive);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        0.0f,
        fixture.app->state().actualLevels.value[0]
    );

    fillRtc(2026U, 9U, 8U, 10U, 1U, 0U, 0U);
    TEST_ASSERT_TRUE(
        fixture.app->update(1000U + STANDARD_TRANSITION_MS)
    );
    TEST_ASSERT_FALSE(fixture.app->state().transitionActive);
    TEST_ASSERT_GREATER_THAN_FLOAT(
        0.0f,
        fixture.app->state().actualLevels.value[0]
    );
}

void test_repeated_begin_resets_core_to_normal() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));

    ChannelLevels manual {};
    manual.value[0] = 50.0f;
    fixture.app->core().enterManual(manual, 0U, 1U);
    TEST_ASSERT_TRUE(fixture.app->update(1U));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperatingMode::Manual),
        static_cast<uint8_t>(fixture.app->state().mode)
    );

    TEST_ASSERT_TRUE(fixture.app->begin(2U));
    assertNormal(*fixture.app);
}

void test_hardware_begin_failure_blocks_normal_operation() {
    Fixture fixture;
    fixture.hardware.beginResult = false;

    TEST_ASSERT_FALSE(fixture.app->begin(0U));
    TEST_ASSERT_FALSE(fixture.app->isRunning());
    TEST_ASSERT_FALSE(fixture.app->status().hardwareOk);
    TEST_ASSERT_FALSE(fixture.app->status().coreOk);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, fixture.hardware.allOffCalls);
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.hardware.setCalls);
    TEST_ASSERT_FALSE(fixture.app->update(1U));
}

void test_pwm_write_failure_enters_fail_safe() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    fixture.hardware.failWriteChannel = 2U;

    TEST_ASSERT_FALSE(fixture.app->update(0U));
    TEST_ASSERT_FALSE(fixture.app->isRunning());
    TEST_ASSERT_FALSE(fixture.app->status().hardwareOk);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, fixture.hardware.allOffCalls);
}

void test_nvs_configuration_is_used() {
    makeNvsConfig(configA);
    saveConfigToMockNvs(configA);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_EQUAL_UINT8(2U, fixture.app->config().activeProfileIndex);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        90.0f,
        fixture.app->config().globalPowerLimitPercent
    );
    TEST_ASSERT_TRUE(fixture.hardware.capturedInversion[0]);
}

void test_nvs_configuration_is_not_modified_during_boot() {
    makeNvsConfig(configA);
    std::memcpy(&configB, &configA, sizeof(DeviceConfig));
    saveConfigToMockNvs(configA);
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_EQUAL_MEMORY(
        &configB,
        &fixture.app->config(),
        sizeof(DeviceConfig)
    );
}

void test_defaults_are_not_saved_automatically() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));

    TEST_ASSERT_EQUAL_UINT32(0U, Preferences::length("cfg_a"));
    TEST_ASSERT_EQUAL_UINT32(0U, Preferences::length("cfg_b"));
}

void test_absent_ntp_does_not_block_start() {
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->isRunning());
    TEST_ASSERT_TRUE(fixture.app->update(0U));
}

void test_absent_wifi_does_not_block_start() {
    Fixture fixture;

    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->status().coreOk);
    TEST_ASSERT_TRUE(fixture.app->status().hardwareOk);
    TEST_ASSERT_TRUE(fixture.app->update(0U));
}

void test_loop_update_is_non_blocking() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    const int rtcCallsAfterBegin = Wire.endTransmissionCalls();

    for (uint16_t iteration = 0; iteration < 100U; ++iteration) {
        TEST_ASSERT_TRUE(fixture.app->update(100U));
    }

    TEST_ASSERT_EQUAL_INT(
        rtcCallsAfterBegin,
        Wire.endTransmissionCalls()
    );
    TEST_ASSERT_TRUE(fixture.app->isRunning());
}

void test_rtc_is_read_at_one_hertz_not_each_loop_iteration() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    const int callsAfterBegin = Wire.endTransmissionCalls();

    TEST_ASSERT_TRUE(fixture.app->update(0U));
    TEST_ASSERT_TRUE(fixture.app->update(100U));
    TEST_ASSERT_TRUE(fixture.app->update(999U));
    TEST_ASSERT_EQUAL_INT(
        callsAfterBegin,
        Wire.endTransmissionCalls()
    );

    TEST_ASSERT_TRUE(fixture.app->update(1000U));
    TEST_ASSERT_EQUAL_INT(
        callsAfterBegin + 2,
        Wire.endTransmissionCalls()
    );
}

void test_local_time_is_passed_to_core() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->update(0U));

    TEST_ASSERT_TRUE(fixture.app->state().timeValid);
    TEST_ASSERT_EQUAL_UINT16(
        12U * 60U,
        fixture.app->state().currentMinuteOfDay
    );
}

void test_actual_levels_are_written_to_hardware() {
    makeNvsConfig(configA);
    saveConfigToMockNvs(configA);
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));

    TEST_ASSERT_TRUE(fixture.app->update(0U));
    fillRtc(2026U, 9U, 8U, 10U, 1U, 0U, 0U);
    TEST_ASSERT_TRUE(fixture.app->update(STANDARD_TRANSITION_MS));

    TEST_ASSERT_GREATER_THAN_UINT32(0U, fixture.hardware.setCalls);

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        TEST_ASSERT_FLOAT_WITHIN(
            0.001f,
            fixture.app->state().actualLevels.value[channel],
            fixture.hardware.lastPercent[channel]
        );
    }
}

void test_new_firmware_instance_after_special_mode_starts_normal() {
    {
        Fixture first;
        TEST_ASSERT_TRUE(first.app->begin(0U));
        ChannelLevels manual {};
        manual.value[0] = 33.0f;
        first.app->core().enterManual(manual, 0U, 1U);
        TEST_ASSERT_TRUE(first.app->update(1U));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(OperatingMode::Manual),
            static_cast<uint8_t>(first.app->state().mode)
        );
    }

    Fixture restarted;
    TEST_ASSERT_TRUE(restarted.app->begin(0U));
    assertNormal(*restarted.app);
}

void test_runtime_hardware_invalid_state_forces_safe_off() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.app->begin(0U));
    TEST_ASSERT_TRUE(fixture.app->update(0U));
    const uint16_t offBefore = fixture.hardware.allOffCalls;

    fixture.hardware.invalidateRuntime();
    TEST_ASSERT_FALSE(fixture.app->update(1U));
    TEST_ASSERT_FALSE(fixture.app->isRunning());
    TEST_ASSERT_FALSE(fixture.app->status().hardwareOk);
    TEST_ASSERT_GREATER_THAN_UINT16(
        offBefore,
        fixture.hardware.allOffCalls
    );
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_hardware_rtc_and_nvs_config_start_in_normal);
    RUN_TEST(test_empty_storage_uses_defaults);
    RUN_TEST(test_storage_begin_failure_uses_defaults);
    RUN_TEST(test_invalid_storage_record_uses_defaults);
    RUN_TEST(test_factory_defaults_pass_validator);
    RUN_TEST(test_invalid_rtc_keeps_outputs_off);
    RUN_TEST(test_rtc_recovery_starts_sixty_second_transition_from_zero);
    RUN_TEST(test_repeated_begin_resets_core_to_normal);
    RUN_TEST(test_hardware_begin_failure_blocks_normal_operation);
    RUN_TEST(test_pwm_write_failure_enters_fail_safe);
    RUN_TEST(test_nvs_configuration_is_used);
    RUN_TEST(test_nvs_configuration_is_not_modified_during_boot);
    RUN_TEST(test_defaults_are_not_saved_automatically);
    RUN_TEST(test_absent_ntp_does_not_block_start);
    RUN_TEST(test_absent_wifi_does_not_block_start);
    RUN_TEST(test_loop_update_is_non_blocking);
    RUN_TEST(test_rtc_is_read_at_one_hertz_not_each_loop_iteration);
    RUN_TEST(test_local_time_is_passed_to_core);
    RUN_TEST(test_actual_levels_are_written_to_hardware);
    RUN_TEST(test_new_firmware_instance_after_special_mode_starts_normal);
    RUN_TEST(test_runtime_hardware_invalid_state_forces_safe_off);

    UNITY_END();
}

void loop() {
}