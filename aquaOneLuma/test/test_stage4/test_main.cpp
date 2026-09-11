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
constexpr uint32_t SIMULATION_DURATION_MS = 60UL * 1000UL;

DeviceConfig makeConfig() {
    DeviceConfig config {};
    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;
    config.globalPowerLimitPercent = 100.0f;

    for (uint8_t profileIndex = 0; profileIndex < PROFILE_COUNT; ++profileIndex) {
        Profile& profile = config.profiles[profileIndex];
        profile.dayStartMinute = 480;
        profile.dayEndMinute = 600;

        for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
            profile.stages[stage].levels.value[0] =
                10.0f + static_cast<float>(stage) * 10.0f;
        }
    }

    return config;
}

LocalTime daytime() {
    LocalTime time {};
    time.valid = true;
    time.hour = 9;
    time.minute = 0;
    time.second = 0;
    time.minuteOfDay = 540;
    return time;
}

float expectedSimulationLevel(
    const Profile& profile,
    float progress
) {
    if (progress >= 1.0f) {
        return profile
            .stages[DAY_STAGE_COUNT - 1]
            .levels.value[0];
    }

    const uint32_t startSecond =
        static_cast<uint32_t>(profile.dayStartMinute) * 60UL;
    const uint32_t endSecond =
        static_cast<uint32_t>(profile.dayEndMinute) * 60UL;
    const uint32_t photoperiodSeconds =
        endSecond - startSecond;
    const uint32_t simulatedSecond =
        startSecond +
        static_cast<uint32_t>(
            progress * static_cast<float>(photoperiodSeconds)
        );

    DayEngine engine;
    return engine
        .calculateSeconds(profile, simulatedSecond)
        .levels.value[0];
}

void assertMode(OperatingMode expected, const LumaCore& core) {
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

void beginSimulationDuringStartup(
    LumaCore& core,
    DeviceConfig& config,
    uint32_t startedMs
) {
    core.begin(config, startedMs);
    core.enterSimulation(1U, startedMs);
    core.update(daytime(), startedMs);
}

void finishStartupEntry(
    LumaCore& core,
    uint32_t startedMs
) {
    core.update(
        daytime(),
        startedMs + STANDARD_TRANSITION_MS
    );
}

void test_simulation_does_not_start_during_active_entry_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const float dayStart =
        config.profiles[0].stages[0].levels.value[0];

    beginSimulationDuringStartup(core, config, 1000U);

    core.update(daytime(), 1000U + 30000U);

    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);
    assertRequested(dayStart, core);
}

void test_simulation_starts_when_entry_transition_actually_finishes() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 2000U;

    beginSimulationDuringStartup(core, config, startedMs);
    finishStartupEntry(core, startedMs);

    TEST_ASSERT_FALSE(core.state().transitionActive);
    assertRequested(10.0f, core);
    assertActual(10.0f, core);

    core.update(
        daytime(),
        startedMs + STANDARD_TRANSITION_MS + 30000U
    );

    const float expected =
        expectedSimulationLevel(config.profiles[0], 0.5f);
    assertRequested(expected, core);
    assertActual(expected, core);
}

void test_startup_sixty_seconds_is_the_real_simulation_entry() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 3000U;

    beginSimulationDuringStartup(core, config, startedMs);

    core.update(
        daytime(),
        startedMs + SIMULATION_ENTRY_MS + 10000U
    );

    assertRequested(10.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    finishStartupEntry(core, startedMs);
    TEST_ASSERT_FALSE(core.state().transitionActive);
    assertRequested(10.0f, core);
}

void test_entry_during_existing_transition_waits_for_replacement_transition() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t enteredMs = 10000U;

    core.begin(config, 0U);
    core.update(daytime(), 0U);
    core.update(daytime(), enteredMs);
    const float beforeEntry = core.state().actualLevels.value[0];

    core.enterSimulation(1U, enteredMs);
    core.update(daytime(), enteredMs);
    assertActual(beforeEntry, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(daytime(), enteredMs + SIMULATION_ENTRY_MS - 1U);
    assertRequested(10.0f, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);

    core.update(daytime(), enteredMs + SIMULATION_ENTRY_MS);
    assertRequested(10.0f, core);
    assertActual(10.0f, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);

    core.update(
        daytime(),
        enteredMs + SIMULATION_ENTRY_MS + 30000U
    );
    assertRequested(
        expectedSimulationLevel(config.profiles[0], 0.5f),
        core
    );
}

void test_progress_zero_is_exact_day_start() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 4000U;

    beginSimulationDuringStartup(core, config, startedMs);
    finishStartupEntry(core, startedMs);

    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_EQUAL_UINT8(0U, core.state().currentStageIndex);
    TEST_ASSERT_EQUAL_UINT8(1U, core.state().nextStageIndex);
    assertRequested(
        config.profiles[0].stages[0].levels.value[0],
        core
    );
}

void test_final_frame_is_exact_sunset() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 5000U;
    const uint32_t simulationStartedMs =
        startedMs + STANDARD_TRANSITION_MS;

    beginSimulationDuringStartup(core, config, startedMs);
    finishStartupEntry(core, startedMs);
    core.update(
        daytime(),
        simulationStartedMs + SIMULATION_DURATION_MS
    );

    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DayState::Day),
        static_cast<uint8_t>(core.state().dayState)
    );
    TEST_ASSERT_EQUAL_UINT8(
        DAY_STAGE_COUNT - 1,
        core.state().currentStageIndex
    );
    TEST_ASSERT_EQUAL_UINT8(
        DAY_STAGE_COUNT - 1,
        core.state().nextStageIndex
    );
    assertRequested(80.0f, core);
    assertActual(80.0f, core);
}

void test_simulation_does_not_exit_before_or_during_final_frame() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 6000U;
    const uint32_t simulationStartedMs =
        startedMs + STANDARD_TRANSITION_MS;

    beginSimulationDuringStartup(core, config, startedMs);
    finishStartupEntry(core, startedMs);

    core.update(
        daytime(),
        simulationStartedMs + SIMULATION_DURATION_MS - 1U
    );
    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_TRUE(core.state().currentStageIndex < DAY_STAGE_COUNT - 1);

    core.update(
        daytime(),
        simulationStartedMs + SIMULATION_DURATION_MS
    );
    assertMode(OperatingMode::Simulation, core);
    assertRequested(80.0f, core);
}

void test_auto_exit_occurs_on_update_after_final_frame_without_jump() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 7000U;
    const uint32_t simulationStartedMs =
        startedMs + STANDARD_TRANSITION_MS;
    const uint32_t finalFrameMs =
        simulationStartedMs + SIMULATION_DURATION_MS;

    beginSimulationDuringStartup(core, config, startedMs);
    finishStartupEntry(core, startedMs);
    core.update(daytime(), finalFrameMs);
    const float sunsetOutput = core.state().actualLevels.value[0];

    core.update(daytime(), finalFrameMs + 1U);

    assertMode(OperatingMode::Normal, core);
    TEST_ASSERT_TRUE(core.state().transitionActive);
    assertActual(sunsetOutput, core);
}

void test_auto_exit_uses_simulation_return_mode() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 8000U;
    const uint32_t finalFrameMs =
        startedMs +
        STANDARD_TRANSITION_MS +
        SIMULATION_DURATION_MS;

    core.begin(config, startedMs);
    core.enterService();
    core.enterSimulation(1U, startedMs);
    core.update(daytime(), startedMs);
    core.update(daytime(), startedMs + STANDARD_TRANSITION_MS);
    core.update(daytime(), finalFrameMs);

    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperatingMode::Service),
        static_cast<uint8_t>(core.state().returnMode)
    );

    core.update(daytime(), finalFrameMs + 1U);
    assertMode(OperatingMode::Service, core);
}

void test_simulation_timing_handles_millis_overflow() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = UINT32_MAX - 30000U;
    const uint32_t simulationStartedMs =
        startedMs + STANDARD_TRANSITION_MS;
    const uint32_t finalFrameMs =
        simulationStartedMs + SIMULATION_DURATION_MS;

    beginSimulationDuringStartup(core, config, startedMs);
    core.update(daytime(), simulationStartedMs);
    assertRequested(10.0f, core);

    core.update(daytime(), finalFrameMs);
    assertMode(OperatingMode::Simulation, core);
    assertRequested(80.0f, core);

    core.update(daytime(), finalFrameMs + 1U);
    assertMode(OperatingMode::Normal, core);
}

void test_simulation_preview_return_preserves_progress_and_continuity() {
    DeviceConfig config = makeConfig();
    LumaCore core;
    const uint32_t startedMs = 9000U;
    const uint32_t simulationStartedMs =
        startedMs + STANDARD_TRANSITION_MS;
    const uint32_t previewMs =
        simulationStartedMs + 10000U;

    core.begin(config, startedMs);
    core.enterSimulation(5U, startedMs);
    core.update(daytime(), startedMs);
    core.update(daytime(), simulationStartedMs);
    core.update(daytime(), previewMs);

    const float beforePreview = core.state().actualLevels.value[0];

    core.enterPreview(config.activeProfileIndex, 7U);
    core.update(daytime(), previewMs);
    assertMode(OperatingMode::Preview, core);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperatingMode::Simulation),
        static_cast<uint8_t>(core.state().returnMode)
    );
    assertActual(beforePreview, core);

    core.exitPreview();
    core.update(daytime(), previewMs);
    assertMode(OperatingMode::Simulation, core);
    assertActual(beforePreview, core);

    core.update(daytime(), previewMs + SIMULATION_ENTRY_MS);
    assertMode(OperatingMode::Simulation, core);
    TEST_ASSERT_FALSE(core.state().transitionActive);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_simulation_does_not_start_during_active_entry_transition);
    RUN_TEST(test_simulation_starts_when_entry_transition_actually_finishes);
    RUN_TEST(test_startup_sixty_seconds_is_the_real_simulation_entry);
    RUN_TEST(test_entry_during_existing_transition_waits_for_replacement_transition);
    RUN_TEST(test_progress_zero_is_exact_day_start);
    RUN_TEST(test_final_frame_is_exact_sunset);
    RUN_TEST(test_simulation_does_not_exit_before_or_during_final_frame);
    RUN_TEST(test_auto_exit_occurs_on_update_after_final_frame_without_jump);
    RUN_TEST(test_auto_exit_uses_simulation_return_mode);
    RUN_TEST(test_simulation_timing_handles_millis_overflow);
    RUN_TEST(test_simulation_preview_return_preserves_progress_and_continuity);

    UNITY_END();
}

void loop() {
}