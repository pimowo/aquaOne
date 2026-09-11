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

constexpr float EPSILON = 0.002f;

DeviceConfig makeConfig() {
    DeviceConfig config {};

    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;
    config.globalPowerLimitPercent = 100.0f;

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        config.channels[channel].enabled = true;
        config.channels[channel].hardMaxPercent = 100.0f;
        config.channels[channel].calibrationMinPercent = 0.0f;
        config.channels[channel].calibrationMaxPercent = 100.0f;
        config.channels[channel].gamma = 1.0f;
    }

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        Profile& current = config.profiles[profile];
        current.dayStartMinute = 480;
        current.dayEndMinute = 600;
        current.nightEnabled = false;

        for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
            current.stages[stage].levels.value[0] =
                10.0f +
                static_cast<float>(profile) * 10.0f +
                static_cast<float>(stage) * 5.0f;
        }
    }

    return config;
}

LocalTime timeAtSecond(uint32_t secondOfDay) {
    LocalTime time {};
    time.valid = true;
    time.hour = static_cast<uint8_t>(secondOfDay / 3600UL);
    time.minute = static_cast<uint8_t>((secondOfDay / 60UL) % 60UL);
    time.second = static_cast<uint8_t>(secondOfDay % 60UL);
    time.minuteOfDay = static_cast<uint16_t>(secondOfDay / 60UL);
    return time;
}

LocalTime dayStart() {
    return timeAtSecond(8UL * 3600UL);
}

void assertMode(
    OperatingMode expected,
    const LumaCore& core
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(core.state().mode)
    );
}

void assertRequested(float expected, const LumaCore& core) {
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        expected,
        core.state().requestedLevels.value[0]
    );
}

void assertActual(float expected, const LumaCore& core) {
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        expected,
        core.state().actualLevels.value[0]
    );
}

void settleNormal(
    LumaCore& core,
    DeviceConfig& config,
    uint32_t startedMs = 0U
) {
    TEST_ASSERT_TRUE(core.begin(config, startedMs));
    core.update(dayStart(), startedMs);
    core.update(dayStart(), startedMs + STANDARD_TRANSITION_MS);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void settlePreview(
    LumaCore& core,
    uint32_t enteredMs
) {
    core.update(dayStart(), enteredMs);
    core.update(dayStart(), enteredMs + PREVIEW_SMOOTH_MS);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_preview_active_profile() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterPreview(config.activeProfileIndex, 3U);
    core.update(dayStart(), enteredMs);

    assertMode(OperatingMode::Preview, core);
    assertRequested(25.0f, core);
    assertActual(10.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), enteredMs + PREVIEW_SMOOTH_MS);
    assertActual(25.0f, core);
}

void test_preview_inactive_profile() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterPreview(3U, 2U);
    core.update(dayStart(), enteredMs);

    assertRequested(50.0f, core);
    TEST_ASSERT_EQUAL_UINT8(0U, config.activeProfileIndex);

    core.update(dayStart(), enteredMs + PREVIEW_SMOOTH_MS);
    assertActual(50.0f, core);
}

void test_change_preview_profile_starts_smooth_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterPreview(1U, 2U);
    settlePreview(core, enteredMs);
    assertActual(30.0f, core);

    const uint32_t changedMs = enteredMs + PREVIEW_SMOOTH_MS + 1U;
    core.setPreviewProfile(3U);
    core.update(dayStart(), changedMs);

    assertRequested(50.0f, core);
    assertActual(30.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), changedMs + PREVIEW_SMOOTH_MS);
    assertActual(50.0f, core);
}

void test_change_preview_stage_starts_smooth_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterPreview(2U, 1U);
    settlePreview(core, enteredMs);
    assertActual(35.0f, core);

    const uint32_t changedMs = enteredMs + PREVIEW_SMOOTH_MS + 1U;
    core.setPreviewStage(6U);
    core.update(dayStart(), changedMs);

    assertRequested(60.0f, core);
    assertActual(35.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), changedMs + PREVIEW_SMOOTH_MS);
    assertActual(60.0f, core);
}

void test_edit_current_preview_stage_starts_smooth_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterPreview(2U, 4U);
    settlePreview(core, enteredMs);
    assertActual(50.0f, core);

    const uint32_t changedMs = enteredMs + PREVIEW_SMOOTH_MS + 1U;
    config.profiles[2].stages[4].levels.value[0] = 88.0f;
    core.update(dayStart(), changedMs);

    assertRequested(88.0f, core);
    assertActual(50.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), changedMs + PREVIEW_SMOOTH_MS);
    assertActual(88.0f, core);
}

void test_preview_does_not_modify_profile_selection_or_data() {
    DeviceConfig config = makeConfig();
    const uint8_t activeBefore = config.activeProfileIndex;
    const uint8_t serviceBefore = config.serviceProfileIndex;
    const float levelBefore =
        config.profiles[4].stages[7].levels.value[0];

    LumaCore core;
    settleNormal(core, config);
    core.enterPreview(4U, 7U);
    core.setPreviewProfile(3U);
    core.setPreviewStage(6U);

    TEST_ASSERT_EQUAL_UINT8(activeBefore, config.activeProfileIndex);
    TEST_ASSERT_EQUAL_UINT8(serviceBefore, config.serviceProfileIndex);
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        levelBefore,
        config.profiles[4].stages[7].levels.value[0]
    );
}

void test_exit_preview_returns_to_service() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    TEST_ASSERT_TRUE(core.begin(config, 0U));

    core.enterService();
    assertMode(OperatingMode::Service, core);

    core.enterPreview(4U, 2U);
    assertMode(OperatingMode::Preview, core);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperatingMode::Service),
        static_cast<uint8_t>(core.state().returnMode)
    );

    core.exitPreview();
    assertMode(OperatingMode::Service, core);
}

void test_edit_active_profile_same_index_starts_smooth_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);
    assertActual(10.0f, core);

    const uint32_t changedMs = STANDARD_TRANSITION_MS + 1U;
    config.profiles[0].stages[0].levels.value[0] = 70.0f;
    core.update(dayStart(), changedMs);

    TEST_ASSERT_EQUAL_UINT8(0U, config.activeProfileIndex);
    assertRequested(70.0f, core);
    assertActual(10.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), changedMs + STANDARD_TRANSITION_MS);
    assertActual(70.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_edit_service_profile_same_index_starts_smooth_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleNormal(core, config);

    const uint32_t serviceEnteredMs = STANDARD_TRANSITION_MS + 1U;
    core.enterService();
    core.update(dayStart(), serviceEnteredMs);
    core.update(dayStart(), serviceEnteredMs + STANDARD_TRANSITION_MS);
    assertActual(20.0f, core);

    const uint32_t changedMs =
        serviceEnteredMs + STANDARD_TRANSITION_MS + 1U;
    config.profiles[1].stages[0].levels.value[0] = 75.0f;
    core.update(dayStart(), changedMs);

    assertMode(OperatingMode::Service, core);
    assertRequested(75.0f, core);
    assertActual(20.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), changedMs + STANDARD_TRANSITION_MS);
    assertActual(75.0f, core);
}

void test_natural_dayengine_motion_does_not_restart_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    TEST_ASSERT_TRUE(core.begin(config, 0U));

    const uint32_t dayStartSecond = 8UL * 3600UL;

    for (uint32_t elapsedMs = 0U;
         elapsedMs <= STANDARD_TRANSITION_MS;
         elapsedMs += 10000U) {
        core.update(
            timeAtSecond(dayStartSecond + elapsedMs / 1000UL),
            elapsedMs
        );
    }

    TEST_ASSERT_FALSE(core.state().transitionActive);
    TEST_ASSERT_TRUE(core.state().requestedLevels.value[0] > 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        core.state().requestedLevels.value[0],
        core.state().actualLevels.value[0]
    );
}

void test_profile_edit_during_active_transition_has_no_jump() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    TEST_ASSERT_TRUE(core.begin(config, 0U));

    core.update(dayStart(), 0U);
    core.update(dayStart(), 30000U);

    const float beforeEdit =
        core.state().actualLevels.value[0];

    config.profiles[0].stages[0].levels.value[0] = 80.0f;
    core.update(dayStart(), 30000U);

    assertActual(beforeEdit, core);
    assertRequested(80.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(dayStart(), 30000U + STANDARD_TRANSITION_MS);
    assertActual(80.0f, core);
}

void test_preview_keeps_manual_channel_test_and_simulation_priority() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    TEST_ASSERT_TRUE(core.begin(config, 0U));

    core.enterSimulation(5U, 1U);
    assertMode(OperatingMode::Simulation, core);

    core.enterPreview(2U, 3U);
    assertMode(OperatingMode::Preview, core);

    ChannelLevels manual {};
    manual.value[0] = 55.0f;
    core.enterManual(manual, 0U, 2U);
    assertMode(OperatingMode::Manual, core);

    core.enterChannelTest(0U, 65.0f);
    assertMode(OperatingMode::ChannelTest, core);

    core.exitChannelTest();
    assertMode(OperatingMode::Manual, core);

    core.exitManual();
    assertMode(OperatingMode::Preview, core);

    core.exitPreview();
    assertMode(OperatingMode::Simulation, core);

    core.exitSimulation();
    assertMode(OperatingMode::Normal, core);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_preview_active_profile);
    RUN_TEST(test_preview_inactive_profile);
    RUN_TEST(test_change_preview_profile_starts_smooth_transition);
    RUN_TEST(test_change_preview_stage_starts_smooth_transition);
    RUN_TEST(test_edit_current_preview_stage_starts_smooth_transition);
    RUN_TEST(test_preview_does_not_modify_profile_selection_or_data);
    RUN_TEST(test_exit_preview_returns_to_service);
    RUN_TEST(test_edit_active_profile_same_index_starts_smooth_transition);
    RUN_TEST(test_edit_service_profile_same_index_starts_smooth_transition);
    RUN_TEST(test_natural_dayengine_motion_does_not_restart_transition);
    RUN_TEST(test_profile_edit_during_active_transition_has_no_jump);
    RUN_TEST(test_preview_keeps_manual_channel_test_and_simulation_priority);

    UNITY_END();
}

void loop() {
}
