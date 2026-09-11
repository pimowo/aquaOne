#include <Arduino.h>
#include <unity.h>

#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/core/LumaCore.cpp"

using namespace LumaSense;

namespace {

DeviceConfig makeConfig() {
    DeviceConfig config {};
    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        config.profiles[profile].dayStartMinute = 480;
        config.profiles[profile].dayEndMinute = 1140;

        for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
            config.profiles[profile]
                .stages[stage]
                .levels.value[0] =
                    static_cast<float>(stage * 10U);
        }
    }

    return config;
}

LocalTime validTime() {
    LocalTime time {};
    time.valid = true;
    time.hour = 12;
    time.minute = 0;
    time.second = 0;
    time.minuteOfDay = 720;
    return time;
}

void assertMode(
    OperatingMode expected,
    OperatingMode actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void test_normal_manual_normal() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterManual());
    assertMode(OperatingMode::Manual, manager.mode());
    assertMode(OperatingMode::Normal, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitManual());
    assertMode(OperatingMode::Normal, manager.mode());
    assertMode(OperatingMode::Normal, manager.returnMode());
}

void test_service_manual_service() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterService());
    TEST_ASSERT_TRUE(manager.enterManual());

    assertMode(OperatingMode::Manual, manager.mode());
    assertMode(OperatingMode::Service, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitManual());
    assertMode(OperatingMode::Service, manager.mode());
    assertMode(OperatingMode::Service, manager.returnMode());
}

void test_manual_channel_test_manual() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterManual());
    TEST_ASSERT_TRUE(manager.enterChannelTest());

    assertMode(OperatingMode::ChannelTest, manager.mode());
    assertMode(OperatingMode::Manual, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitChannelTest());
    assertMode(OperatingMode::Manual, manager.mode());
}

void test_manual_timeout_survives_channel_test() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const LocalTime time = validTime();
    const uint32_t startedMs = 1000U;

    ChannelLevels manual {};
    manual.value[0] = 35.0f;

    core.begin(config, startedMs);
    core.enterManual(manual, 15, startedMs);
    core.enterChannelTest(1, 60.0f);

    core.exitManual();

    assertMode(OperatingMode::ChannelTest, core.state().mode);
    assertMode(OperatingMode::Manual, core.state().returnMode);

    core.exitChannelTest();
    assertMode(OperatingMode::Manual, core.state().mode);

    core.update(
        time,
        startedMs + 15UL * 60UL * 1000UL + 1UL
    );

    assertMode(OperatingMode::Normal, core.state().mode);
}

void test_expired_manual_does_not_resume_after_channel_timeout() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    LocalTime time = validTime();
    const uint32_t manualStarted = 1000U;
    const uint32_t channelStarted = manualStarted + 11U * 60U * 1000U;

    core.begin(config, manualStarted);
    core.enterManual({}, 15U, manualStarted);
    core.enterChannelTest(0U, 50U);
    core.update(time, channelStarted);
    core.update(time, channelStarted + CHANNEL_TEST_TIMEOUT_MS + 1U);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperatingMode::Normal), static_cast<uint8_t>(core.state().mode));
}
void test_preview_channel_test() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterPreview());
    TEST_ASSERT_TRUE(manager.enterChannelTest());

    assertMode(OperatingMode::ChannelTest, manager.mode());
    assertMode(OperatingMode::Preview, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitChannelTest());
    assertMode(OperatingMode::Preview, manager.mode());
}

void test_simulation_channel_test() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterSimulation());
    TEST_ASSERT_TRUE(manager.enterChannelTest());

    assertMode(OperatingMode::ChannelTest, manager.mode());
    assertMode(OperatingMode::Simulation, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitChannelTest());
    assertMode(OperatingMode::Simulation, manager.mode());
}

void test_simulation_preview() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterSimulation());
    TEST_ASSERT_TRUE(manager.enterPreview());

    assertMode(OperatingMode::Preview, manager.mode());
    assertMode(OperatingMode::Simulation, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitPreview());
    assertMode(OperatingMode::Simulation, manager.mode());
}

void test_full_priority_chain_and_returns() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterSimulation());
    TEST_ASSERT_TRUE(manager.enterPreview());
    TEST_ASSERT_TRUE(manager.enterManual());
    TEST_ASSERT_TRUE(manager.enterChannelTest());

    assertMode(OperatingMode::ChannelTest, manager.mode());
    assertMode(OperatingMode::Manual, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitChannelTest());
    assertMode(OperatingMode::Manual, manager.mode());
    assertMode(OperatingMode::Preview, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitManual());
    assertMode(OperatingMode::Preview, manager.mode());
    assertMode(OperatingMode::Simulation, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitPreview());
    assertMode(OperatingMode::Simulation, manager.mode());
    assertMode(OperatingMode::Normal, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitSimulation());
    assertMode(OperatingMode::Normal, manager.mode());
}

void test_service_change_updates_return_mode() {
    ModeManager manager;

    TEST_ASSERT_TRUE(manager.enterService());
    TEST_ASSERT_TRUE(manager.enterManual());

    assertMode(OperatingMode::Service, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitService());

    assertMode(OperatingMode::Manual, manager.mode());
    assertMode(OperatingMode::Normal, manager.returnMode());

    TEST_ASSERT_TRUE(manager.exitManual());
    assertMode(OperatingMode::Normal, manager.mode());
}

void test_repeated_and_rejected_commands() {
    ModeManager manager;

    TEST_ASSERT_FALSE(manager.exitOff());
    TEST_ASSERT_FALSE(manager.exitOff());

    TEST_ASSERT_TRUE(manager.enterSimulation());
    TEST_ASSERT_FALSE(manager.enterSimulation());

    TEST_ASSERT_TRUE(manager.enterPreview());
    TEST_ASSERT_FALSE(manager.enterPreview());
    TEST_ASSERT_FALSE(manager.enterSimulation());

    TEST_ASSERT_TRUE(manager.enterManual());
    TEST_ASSERT_FALSE(manager.enterManual());
    TEST_ASSERT_FALSE(manager.enterPreview());

    TEST_ASSERT_TRUE(manager.enterChannelTest());
    TEST_ASSERT_FALSE(manager.enterChannelTest());
    TEST_ASSERT_FALSE(manager.enterManual());
    TEST_ASSERT_FALSE(manager.exitManual());
}

void test_repeated_enter_manual_keeps_data_and_timeout() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const LocalTime time = validTime();
    const uint32_t startedMs = 5000U;

    ChannelLevels first {};
    first.value[0] = 20.0f;

    ChannelLevels repeated {};
    repeated.value[0] = 90.0f;

    core.begin(config, startedMs);
    core.enterManual(first, 15, startedMs);
    core.enterManual(repeated, 60, startedMs + 1000U);

    core.update(time, startedMs + 2000U);

    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        20.0f,
        core.state().requestedLevels.value[0]
    );

    core.update(
        time,
        startedMs + 15UL * 60UL * 1000UL + 1UL
    );

    assertMode(OperatingMode::Normal, core.state().mode);
}

void test_repeated_channel_test_keeps_parameters() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const LocalTime time = validTime();

    core.begin(config, 0U);
    core.enterChannelTest(0, 25.0f);
    core.enterChannelTest(1, 90.0f);
    core.update(time, 1U);

    assertMode(OperatingMode::ChannelTest, core.state().mode);
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        25.0f,
        core.state().requestedLevels.value[0]
    );
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        0.0f,
        core.state().requestedLevels.value[1]
    );
}

void test_exit_off_outside_off_is_idempotent() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const LocalTime time = validTime();

    core.begin(config, 100U);
    core.update(time, 100U);
    core.update(time, 100U + STANDARD_TRANSITION_MS + 1U);

    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.exitOff();
    core.exitOff();
    core.update(time, 100U + STANDARD_TRANSITION_MS + 2U);

    assertMode(OperatingMode::Normal, core.state().mode);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_begin_resets_mode_to_normal() {
    DeviceConfig config = makeConfig();
    LumaCore core;

    core.begin(config, 0U);
    core.enterSimulation(5, 1U);
    core.enterPreview(config.activeProfileIndex, 2);
    core.enterManual(ChannelLevels {}, 15, 2U);

    assertMode(OperatingMode::Manual, core.state().mode);

    core.begin(config, 3U);

    assertMode(OperatingMode::Normal, core.state().mode);
    assertMode(OperatingMode::Normal, core.state().returnMode);
}

void test_manual_timeout_across_millis_overflow() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const LocalTime time = validTime();

    const uint32_t startedMs = UINT32_MAX - 1000U;
    const uint32_t timeoutMs = 15UL * 60UL * 1000UL;
    const uint32_t afterTimeoutMs =
        startedMs + timeoutMs + 1U;

    core.begin(config, startedMs);
    core.enterManual(ChannelLevels {}, 15, startedMs);
    core.update(time, afterTimeoutMs);

    assertMode(OperatingMode::Normal, core.state().mode);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_normal_manual_normal);
    RUN_TEST(test_service_manual_service);
    RUN_TEST(test_manual_channel_test_manual);
    RUN_TEST(test_manual_timeout_survives_channel_test);
    RUN_TEST(test_expired_manual_does_not_resume_after_channel_timeout);
    RUN_TEST(test_preview_channel_test);
    RUN_TEST(test_simulation_channel_test);
    RUN_TEST(test_simulation_preview);
    RUN_TEST(test_full_priority_chain_and_returns);
    RUN_TEST(test_service_change_updates_return_mode);
    RUN_TEST(test_repeated_and_rejected_commands);
    RUN_TEST(test_repeated_enter_manual_keeps_data_and_timeout);
    RUN_TEST(test_repeated_channel_test_keeps_parameters);
    RUN_TEST(test_exit_off_outside_off_is_idempotent);
    RUN_TEST(test_begin_resets_mode_to_normal);
    RUN_TEST(test_manual_timeout_across_millis_overflow);

    UNITY_END();
}

void loop() {
}