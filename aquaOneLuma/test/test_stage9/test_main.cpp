#include <Arduino.h>
#include <unity.h>

#include <stdint.h>

#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/core/LumaCore.cpp"

using namespace LumaSense;

namespace {

constexpr float EPSILON = 0.003f;
constexpr uint32_t DAY_SECONDS = 24UL * 60UL * 60UL;

DeviceConfig makeConfig() {
    DeviceConfig config {};

    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;
    config.globalPowerLimitPercent = 100.0f;

    const float stageLevels[DAY_STAGE_COUNT] = {
        10.0f,
        25.0f,
        40.0f,
        80.0f,
        60.0f,
        45.0f,
        25.0f,
        10.0f
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
            current.stages[stage].levels.value[0] =
                stageLevels[stage];
        }
    }

    return config;
}

LocalTime timeAtSecond(uint32_t secondOfDay) {
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

float targetAt(
    const DeviceConfig& config,
    uint32_t secondOfDay,
    uint8_t profileIndex = 0U
) {
    return DayEngine()
        .calculateSeconds(
            config.profiles[profileIndex],
            secondOfDay
        )
        .levels
        .value[0];
}

void assertActual(float expected, const LumaCore& core) {
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        expected,
        core.state().actualLevels.value[0]
    );
}

void assertRequested(float expected, const LumaCore& core) {
    TEST_ASSERT_FLOAT_WITHIN(
        EPSILON,
        expected,
        core.state().requestedLevels.value[0]
    );
}

void assertDayState(DayState expected, const LumaCore& core) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(core.state().dayState)
    );
}

void settleAt(
    LumaCore& core,
    DeviceConfig& config,
    uint32_t firstSecondOfDay,
    uint32_t startedMs = 0U
) {
    TEST_ASSERT_TRUE(core.begin(config, startedMs));
    core.update(timeAtSecond(firstSecondOfDay), startedMs);
    core.update(
        timeAtSecond(firstSecondOfDay + 60UL),
        startedMs + STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_normal_second_progress_does_not_start_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t firstSecond = secondsAt(10U, 0U);
    settleAt(core, config, firstSecond);

    for (uint32_t offset = 1U; offset <= 5U; ++offset) {
        core.update(
            timeAtSecond(firstSecond + 60UL + offset),
            STANDARD_TRANSITION_MS + offset * 1000UL
        );
        TEST_ASSERT_FALSE(core.state().transitionActive);
    }
}

void test_update_jitter_between_one_and_three_seconds_is_tolerated() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t settledSecond = secondsAt(10U, 1U);
    settleAt(core, config, secondsAt(10U, 0U));

    core.update(
        timeAtSecond(settledSecond + 1U),
        STANDARD_TRANSITION_MS + 1500U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        timeAtSecond(settledSecond + 3U),
        STANDARD_TRANSITION_MS + 3700U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        timeAtSecond(settledSecond + 6U),
        STANDARD_TRANSITION_MS + 6700U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_tolerance_accepts_three_seconds_but_rejects_four() {
    TEST_ASSERT_FALSE(
        isSignificantTimeJump(
            secondsAt(10U, 0U),
            secondsAt(10U, 0U, 4U),
            1000U,
            2000U
        )
    );

    TEST_ASSERT_TRUE(
        isSignificantTimeJump(
            secondsAt(10U, 0U),
            secondsAt(10U, 0U, 5U),
            1000U,
            2000U
        )
    );
}

void test_forward_ten_minute_jump_starts_standard_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t settledSecond = secondsAt(10U, 1U);
    settleAt(core, config, secondsAt(10U, 0U));

    const float beforeJump = core.state().actualLevels.value[0];
    const uint32_t jumpedSecond = settledSecond + 10UL * 60UL;
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertActual(beforeJump, core);
    assertRequested(targetAt(config, jumpedSecond), core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 5U),
        jumpedMs + 5000U
    );
    TEST_ASSERT_TRUE(core.state().transitionActive);
}

void test_backward_ten_minute_jump_starts_standard_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t settledSecond = secondsAt(10U, 31U);
    settleAt(core, config, secondsAt(10U, 30U));

    const float beforeJump = core.state().actualLevels.value[0];
    const uint32_t jumpedSecond = settledSecond - 10UL * 60UL;
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertActual(beforeJump, core);
    assertRequested(targetAt(config, jumpedSecond), core);
    TEST_ASSERT_TRUE(core.state().transitionActive);
}

void test_large_jump_to_another_day_stage_is_smooth() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(9U, 0U));

    const float beforeJump = core.state().actualLevels.value[0];
    const uint32_t jumpedSecond = secondsAt(17U, 0U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertActual(beforeJump, core);
    assertRequested(targetAt(config, jumpedSecond), core);
    TEST_ASSERT_TRUE(core.state().transitionActive);
}

void test_jump_across_midnight_starts_standard_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(18U, 59U));

    const float beforeJump = core.state().actualLevels.value[0];
    const uint32_t jumpedSecond = secondsAt(9U, 0U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertDayState(DayState::Day, core);
    assertActual(beforeJump, core);
    assertRequested(targetAt(config, jumpedSecond), core);
    TEST_ASSERT_TRUE(core.state().transitionActive);
}

void test_normal_midnight_rollover_is_not_a_jump() {
    TEST_ASSERT_FALSE(
        isSignificantTimeJump(
            secondsAt(23U, 59U, 59U),
            secondsAt(0U, 0U, 0U),
            1000U,
            2000U
        )
    );

    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 0U;
    TEST_ASSERT_TRUE(core.begin(config, startedMs));

    core.update(timeAtSecond(secondsAt(23U, 58U, 59U)), startedMs);
    core.update(
        timeAtSecond(secondsAt(23U, 59U, 59U)),
        STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        timeAtSecond(secondsAt(0U, 0U, 0U)),
        STANDARD_TRANSITION_MS + 1000U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_time_detection_handles_millis_overflow() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = UINT32_MAX - 30000U;
    const uint32_t firstSecond = secondsAt(10U, 0U);

    settleAt(core, config, firstSecond, startedMs);

    core.update(
        timeAtSecond(firstSecond + 61UL),
        startedMs + STANDARD_TRANSITION_MS + 1000U
    );

    TEST_ASSERT_FALSE(core.state().transitionActive);
    TEST_ASSERT_FALSE(
        isSignificantTimeJump(
            firstSecond,
            firstSecond + 1UL,
            UINT32_MAX - 500U,
            499U
        )
    );
}

void test_day_to_day_jump_uses_standard_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(11U, 0U));

    const uint32_t jumpedSecond = secondsAt(15U, 0U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;
    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertDayState(DayState::Day, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + NIGHT_TRANSITION_MS / 1000UL),
        jumpedMs + NIGHT_TRANSITION_MS
    );
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + STANDARD_TRANSITION_MS / 1000UL),
        jumpedMs + STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_day_to_night_jump_keeps_night_transition_duration() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(19U, 58U));
    assertDayState(DayState::Day, core);

    const uint32_t jumpedSecond = secondsAt(20U, 10U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;
    const float beforeJump = core.state().actualLevels.value[0];

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertDayState(DayState::Night, core);
    assertActual(beforeJump, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 5U),
        jumpedMs + NIGHT_TRANSITION_MS
    );
    assertActual(2.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_night_to_day_jump_keeps_night_transition_duration() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(7U, 58U));
    assertDayState(DayState::Night, core);

    const uint32_t jumpedSecond = secondsAt(8U, 10U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;
    const float beforeJump = core.state().actualLevels.value[0];

    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertDayState(DayState::Day, core);
    assertActual(beforeJump, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 5U),
        jumpedMs + NIGHT_TRANSITION_MS
    );
    assertActual(targetAt(config, jumpedSecond + 5U), core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_manual_ignores_time_jump_and_return_has_no_false_jump() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t settledSecond = secondsAt(10U, 1U);
    settleAt(core, config, secondsAt(10U, 0U));

    ChannelLevels manual {};
    manual.value[0] = 55.0f;

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1000U;
    core.enterManual(manual, 0U, enteredMs);
    core.update(timeAtSecond(settledSecond + 1U), enteredMs);
    core.update(
        timeAtSecond(settledSecond + 61U),
        enteredMs + STANDARD_TRANSITION_MS
    );
    assertActual(55.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);

    const uint32_t jumpedSecond = secondsAt(18U, 0U);
    const uint32_t jumpedMs = enteredMs + STANDARD_TRANSITION_MS + 1000U;
    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertRequested(55.0f, core);
    assertActual(55.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.exitManual();
    core.update(timeAtSecond(jumpedSecond), jumpedMs);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 60U),
        jumpedMs + STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 61U),
        jumpedMs + STANDARD_TRANSITION_MS + 1000U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_preview_ignores_time_jump() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(10U, 0U));

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1000U;
    core.enterPreview(2U, 3U);
    core.update(timeAtSecond(secondsAt(10U, 1U, 1U)), enteredMs);
    core.update(
        timeAtSecond(secondsAt(10U, 1U, 2U)),
        enteredMs + PREVIEW_SMOOTH_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    const float previewLevel =
        config.profiles[2].stages[3].levels.value[0];
    core.update(
        timeAtSecond(secondsAt(18U, 0U)),
        enteredMs + PREVIEW_SMOOTH_MS + 1000U
    );

    assertRequested(previewLevel, core);
    assertActual(previewLevel, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_simulation_return_has_no_false_time_jump() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    settleAt(core, config, secondsAt(10U, 0U));

    const uint32_t enteredMs = STANDARD_TRANSITION_MS + 1000U;
    core.enterSimulation(5U, enteredMs);
    core.update(timeAtSecond(secondsAt(10U, 1U, 1U)), enteredMs);
    core.update(
        timeAtSecond(secondsAt(10U, 1U, 9U)),
        enteredMs + SIMULATION_ENTRY_MS
    );

    const uint32_t jumpedSecond = secondsAt(18U, 0U);
    const uint32_t exitMs = enteredMs + SIMULATION_ENTRY_MS + 1000U;
    core.update(timeAtSecond(jumpedSecond), exitMs);
    core.exitSimulation();
    core.update(timeAtSecond(jumpedSecond), exitMs);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 8U),
        exitMs + SIMULATION_ENTRY_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        timeAtSecond(jumpedSecond + 9U),
        exitMs + SIMULATION_ENTRY_MS + 1000U
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

void test_invalid_to_valid_still_starts_from_zero_for_60_seconds() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    TEST_ASSERT_TRUE(core.begin(config, 0U));

    core.update(LocalTime {}, 0U);
    assertActual(0.0f, core);
    TEST_ASSERT_FALSE(core.state().timeValid);

    const uint32_t validSecond = secondsAt(12U, 0U);
    core.update(timeAtSecond(validSecond), 1000U);

    TEST_ASSERT_TRUE(core.state().timeValid);
    assertActual(0.0f, core);
    assertRequested(targetAt(config, validSecond), core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(
        timeAtSecond(validSecond + 60U),
        1000U + STANDARD_TRANSITION_MS
    );
    TEST_ASSERT_FALSE(core.state().transitionActive);
    assertActual(targetAt(config, validSecond + 60U), core);
}

void test_time_jump_transition_keeps_dynamic_target_without_restart() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t settledSecond = secondsAt(10U, 1U);
    settleAt(core, config, secondsAt(10U, 0U));

    const float beforeJump = core.state().actualLevels.value[0];
    const uint32_t jumpedSecond = secondsAt(16U, 0U);
    const uint32_t jumpedMs = STANDARD_TRANSITION_MS + 1000U;
    core.update(timeAtSecond(jumpedSecond), jumpedMs);

    assertActual(beforeJump, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    for (uint32_t elapsed = 10000U;
         elapsed < STANDARD_TRANSITION_MS;
         elapsed += 10000U) {
        core.update(
            timeAtSecond(jumpedSecond + elapsed / 1000UL),
            jumpedMs + elapsed
        );
        TEST_ASSERT_TRUE(core.state().transitionActive);
    }

    core.update(
        timeAtSecond(
            jumpedSecond + STANDARD_TRANSITION_MS / 1000UL
        ),
        jumpedMs + STANDARD_TRANSITION_MS
    );

    TEST_ASSERT_FALSE(core.state().transitionActive);
    assertRequested(
        targetAt(
            config,
            jumpedSecond + STANDARD_TRANSITION_MS / 1000UL
        ),
        core
    );
    assertActual(core.state().requestedLevels.value[0], core);

    (void)settledSecond;
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_normal_second_progress_does_not_start_transition);
    RUN_TEST(test_update_jitter_between_one_and_three_seconds_is_tolerated);
    RUN_TEST(test_tolerance_accepts_three_seconds_but_rejects_four);
    RUN_TEST(test_forward_ten_minute_jump_starts_standard_transition);
    RUN_TEST(test_backward_ten_minute_jump_starts_standard_transition);
    RUN_TEST(test_large_jump_to_another_day_stage_is_smooth);
    RUN_TEST(test_jump_across_midnight_starts_standard_transition);
    RUN_TEST(test_normal_midnight_rollover_is_not_a_jump);
    RUN_TEST(test_time_detection_handles_millis_overflow);
    RUN_TEST(test_day_to_day_jump_uses_standard_transition);
    RUN_TEST(test_day_to_night_jump_keeps_night_transition_duration);
    RUN_TEST(test_night_to_day_jump_keeps_night_transition_duration);
    RUN_TEST(test_manual_ignores_time_jump_and_return_has_no_false_jump);
    RUN_TEST(test_preview_ignores_time_jump);
    RUN_TEST(test_simulation_return_has_no_false_time_jump);
    RUN_TEST(test_invalid_to_valid_still_starts_from_zero_for_60_seconds);
    RUN_TEST(test_time_jump_transition_keeps_dynamic_target_without_restart);

    UNITY_END();
}

void loop() {
}
