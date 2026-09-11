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

constexpr float EPSILON = 0.001f;

ChannelLevels levels(float channel0) {
    ChannelLevels result {};
    result.value[0] = channel0;
    return result;
}

float smoothstep(float value) {
    return value * value * (3.0f - 2.0f * value);
}

void assertLevel(float expected, const ChannelLevels& actual) {
    TEST_ASSERT_FLOAT_WITHIN(EPSILON, expected, actual.value[0]);
}

void assertCoreLevel(float expected, const LumaCore& core) {
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        expected,
        core.state().actualLevels.value[0]
    );
}

LocalTime timeAt(uint8_t hour, uint8_t minute, uint8_t second) {
    LocalTime time {};
    time.valid = true;
    time.hour = hour;
    time.minute = minute;
    time.second = second;
    time.minuteOfDay =
        static_cast<uint16_t>(hour) * 60U + minute;
    return time;
}

DeviceConfig makeConfig() {
    DeviceConfig config {};
    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;
    config.globalPowerLimitPercent = 100.0f;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        config.profiles[profile].dayStartMinute = 480;
        config.profiles[profile].dayEndMinute = 1140;

        for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
            config.profiles[profile].stages[stage].levels.value[0] =
                20.0f;
        }
    }

    return config;
}

void test_dynamic_target_mid_transition_keeps_origin_and_clock() {
    TransitionEngine engine;

    engine.start(levels(10.0f), levels(90.0f), 1000U, 100U);

    const ChannelLevels middle =
        engine.update(600U, levels(50.0f));

    assertLevel(30.0f, middle);
    TEST_ASSERT_TRUE(engine.isActive());

    const float expected =
        10.0f + (70.0f - 10.0f) * smoothstep(0.75f);

    assertLevel(expected, engine.update(850U, levels(70.0f)));
    TEST_ASSERT_TRUE(engine.isActive());
}

void test_target_can_change_multiple_times_without_time_restart() {
    TransitionEngine engine;

    engine.start(levels(0.0f), levels(100.0f), 1000U, 0U);

    assertLevel(
        80.0f * smoothstep(0.20f),
        engine.update(200U, levels(80.0f))
    );
    assertLevel(
        60.0f * smoothstep(0.40f),
        engine.update(400U, levels(60.0f))
    );
    assertLevel(
        90.0f * smoothstep(0.70f),
        engine.update(700U, levels(90.0f))
    );

    assertLevel(30.0f, engine.update(1000U, levels(30.0f)));
    TEST_ASSERT_FALSE(engine.isActive());
}

void test_continuous_moving_target_produces_continuous_output() {
    TransitionEngine engine;

    engine.start(levels(0.0f), levels(40.0f), 1000U, 0U);

    const float before =
        engine.update(499U, levels(59.9f)).value[0];
    const float after =
        engine.update(500U, levels(60.0f)).value[0];

    TEST_ASSERT_TRUE(after > before);
    TEST_ASSERT_TRUE(after - before < 0.2f);
}

void test_transition_finishes_at_latest_target_without_jump() {
    TransitionEngine engine;

    engine.start(levels(5.0f), levels(20.0f), 1000U, 0U);
    engine.update(500U, levels(60.0f));

    const ChannelLevels completed =
        engine.update(1000U, levels(75.0f));

    assertLevel(75.0f, completed);
    TEST_ASSERT_FALSE(engine.isActive());
    assertLevel(75.0f, engine.update(1001U));
}

void test_zero_duration_finishes_immediately() {
    TransitionEngine engine;

    engine.start(levels(15.0f), levels(85.0f), 0U, 1234U);

    TEST_ASSERT_FALSE(engine.isActive());
    assertLevel(85.0f, engine.update(1234U));
}

void test_transition_handles_millis_overflow() {
    TransitionEngine engine;
    const uint32_t startedMs = UINT32_MAX - 100U;

    engine.start(levels(0.0f), levels(100.0f), 200U, startedMs);

    assertLevel(50.0f, engine.update(startedMs + 100U));
    TEST_ASSERT_TRUE(engine.isActive());

    assertLevel(80.0f, engine.update(startedMs + 200U, levels(80.0f)));
    TEST_ASSERT_FALSE(engine.isActive());
}

void test_new_transition_uses_current_output_as_from() {
    TransitionEngine engine;

    engine.start(levels(0.0f), levels(100.0f), 1000U, 0U);
    const ChannelLevels interruptedFrom = engine.update(400U);

    assertLevel(35.2f, interruptedFrom);

    engine.start(interruptedFrom, levels(0.0f), 1000U, 400U);

    assertLevel(35.2f, engine.update(400U));
    assertLevel(17.6f, engine.update(900U));
    assertLevel(0.0f, engine.update(1400U));
    TEST_ASSERT_FALSE(engine.isActive());
}

void test_lumacore_short_photoperiod_tracks_schedule_until_completion() {
    DeviceConfig config = makeConfig();
    Profile& profile = config.profiles[0];
    profile.dayStartMinute = 720;
    profile.dayEndMinute = 722;

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        profile.stages[stage].levels.value[0] =
            10.0f + static_cast<float>(stage) * 10.0f;
    }

    LumaCore core;
    const uint32_t startedMs = 1000U;
    core.begin(config, startedMs);

    core.update(timeAt(12, 0, 0), startedMs);
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        10.0f,
        core.state().requestedLevels.value[0]
    );
    assertCoreLevel(0.0f, core);

    core.update(timeAt(12, 0, 30), startedMs + 30000U);
    const float middleTarget = core.state().requestedLevels.value[0];
    TEST_ASSERT_TRUE(middleTarget > 10.0f);
    assertCoreLevel(middleTarget * 0.5f, core);

    core.update(timeAt(12, 1, 0), startedMs + STANDARD_TRANSITION_MS);
    const float completed = core.state().actualLevels.value[0];

    TEST_ASSERT_FALSE(core.state().transitionActive);
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        core.state().requestedLevels.value[0],
        completed
    );

    core.update(timeAt(12, 1, 0), startedMs + STANDARD_TRANSITION_MS + 1U);
    assertCoreLevel(completed, core);
}

void test_lumacore_profile_change_interrupts_from_current_output() {
    DeviceConfig config = makeConfig();

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        config.profiles[0].stages[stage].levels.value[0] = 20.0f;
        config.profiles[1].stages[stage].levels.value[0] = 80.0f;
    }

    LumaCore core;
    const LocalTime noon = timeAt(12, 0, 0);
    core.begin(config, 0U);
    core.update(noon, 0U);
    core.update(noon, 10000U);

    const float beforeProfileChange =
        core.state().actualLevels.value[0];

    config.activeProfileIndex = 1;
    core.update(noon, 10000U);

    assertCoreLevel(beforeProfileChange, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(noon, 10000U + STANDARD_TRANSITION_MS);
    assertCoreLevel(80.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_lumacore_night_profile_edit_interrupts_from_current_output() {
    DeviceConfig config = makeConfig();
    Profile& profile = config.profiles[0];
    profile.dayStartMinute = 720;
    profile.dayEndMinute = 723;
    profile.nightEnabled = true;
    profile.nightLevels.value[0] = 20.0f;

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        profile.stages[stage].levels.value[0] = 80.0f;
    }

    LumaCore core;
    core.begin(config, 0U);
    core.update(timeAt(12, 0, 0), 0U);
    core.update(timeAt(12, 1, 0), STANDARD_TRANSITION_MS);
    assertCoreLevel(80.0f, core);

    const uint32_t nightStartedMs = 180000U;
    core.update(timeAt(12, 3, 0), nightStartedMs);
    assertCoreLevel(80.0f, core);

    // Stage 8 classifies this as a profile-data edit. The edit
    // interrupts the 5 s DAY->NIGHT transition from its current
    // output and starts the standard 60 s smoothing transition.
    profile.nightLevels.value[0] = 40.0f;
    const uint32_t profileEditedMs = nightStartedMs + 2500U;
    core.update(timeAt(12, 3, 2), profileEditedMs);
    assertCoreLevel(50.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    // Reaching the end time of the superseded transition must
    // neither jump to the target nor complete the replacement.
    core.update(
        timeAt(12, 3, 5),
        nightStartedMs + NIGHT_TRANSITION_MS
    );
    TEST_ASSERT_TRUE(
        core.state().actualLevels.value[0] > 40.0f
    );
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAt(12, 4, 2),
        profileEditedMs + STANDARD_TRANSITION_MS
    );
    assertCoreLevel(40.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_mode_transitions_interrupt_without_immediate_output_jump() {
    DeviceConfig config = makeConfig();
    Profile& profile = config.profiles[0];

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        profile.stages[stage].levels.value[0] =
            10.0f + static_cast<float>(stage) * 10.0f;
    }

    LumaCore core;
    const LocalTime noon = timeAt(12, 0, 0);
    const uint32_t nowMs = 100000U;

    core.begin(config, 0U);
    core.update(noon, 0U);
    core.update(noon, STANDARD_TRANSITION_MS);

    float previous = core.state().actualLevels.value[0];

    core.enterSimulation(5U, nowMs);
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.enterPreview(config.activeProfileIndex, 7U);
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.enterManual(levels(90.0f), 0U, nowMs);
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.enterChannelTest(0U, 5.0f);
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.exitChannelTest();
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.exitManual();
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.exitPreview();
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);

    previous = core.state().actualLevels.value[0];
    core.exitSimulation();
    core.update(noon, nowMs);
    assertCoreLevel(previous, core);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_dynamic_target_mid_transition_keeps_origin_and_clock);
    RUN_TEST(test_target_can_change_multiple_times_without_time_restart);
    RUN_TEST(test_continuous_moving_target_produces_continuous_output);
    RUN_TEST(test_transition_finishes_at_latest_target_without_jump);
    RUN_TEST(test_zero_duration_finishes_immediately);
    RUN_TEST(test_transition_handles_millis_overflow);
    RUN_TEST(test_new_transition_uses_current_output_as_from);
    RUN_TEST(test_lumacore_short_photoperiod_tracks_schedule_until_completion);
    RUN_TEST(test_lumacore_profile_change_interrupts_from_current_output);
    RUN_TEST(test_lumacore_night_profile_edit_interrupts_from_current_output);
    RUN_TEST(test_mode_transitions_interrupt_without_immediate_output_jump);

    UNITY_END();
}

void loop() {
}