#include <Arduino.h>
#include <unity.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "../../src/core/ModeManager.cpp"
#include "../../src/core/TransitionEngine.cpp"
#include "../../src/core/LightEngine.cpp"
#include "../../src/profiles/DayEngine.cpp"
#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/core/LumaCore.cpp"

using namespace LumaSense;

namespace {

const float QUIET_NAN =
    std::numeric_limits<float>::quiet_NaN();
const float POSITIVE_INFINITY =
    std::numeric_limits<float>::infinity();

DeviceConfig makeValidConfig() {
    DeviceConfig config {};

    config.activeProfileIndex = 0;
    config.serviceProfileIndex = 1;
    config.globalPowerLimitPercent = 100.0f;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        config.profiles[profile].dayStartMinute = 480;
        config.profiles[profile].dayEndMinute = 1140;
    }

    return config;
}

float processChannel0(
    float requested,
    const DeviceConfig& config
) {
    ChannelLevels levels {};
    levels.value[0] = requested;

    return LightEngine().process(levels, config).value[0];
}

void assertSafePercent(float value) {
    TEST_ASSERT_TRUE(std::isfinite(value));
    TEST_ASSERT_TRUE(value >= 0.0f);
    TEST_ASSERT_TRUE(value <= 100.0f);
}

void test_valid_configuration_passes() {
    const DeviceConfig config = makeValidConfig();

    TEST_ASSERT_TRUE(ConfigValidator::validate(config));
}

void test_profile_indexes_out_of_range_are_rejected() {
    DeviceConfig config = makeValidConfig();
    config.activeProfileIndex = PROFILE_COUNT;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.serviceProfileIndex = PROFILE_COUNT;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_global_limit_invalid_values_are_rejected() {
    const float invalidValues[] = {
        -0.01f,
        100.01f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    for (float value : invalidValues) {
        DeviceConfig config = makeValidConfig();
        config.globalPowerLimitPercent = value;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_hard_max_invalid_values_are_rejected() {
    const float invalidValues[] = {
        -0.01f,
        100.01f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    for (float value : invalidValues) {
        DeviceConfig config = makeValidConfig();
        config.channels[0].hardMaxPercent = value;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_calibration_out_of_range_is_rejected() {
    DeviceConfig config = makeValidConfig();
    config.channels[0].calibrationMinPercent = -0.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].calibrationMinPercent = 100.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].calibrationMaxPercent = -0.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].calibrationMaxPercent = 100.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].calibrationMinPercent = QUIET_NAN;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].calibrationMaxPercent = POSITIVE_INFINITY;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_calibration_min_above_max_is_rejected() {
    DeviceConfig config = makeValidConfig();
    config.channels[0].calibrationMinPercent = 60.0f;
    config.channels[0].calibrationMaxPercent = 40.0f;

    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_gamma_invalid_values_are_rejected() {
    const float invalidValues[] = {
        0.0f,
        -1.0f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    for (float value : invalidValues) {
        DeviceConfig config = makeValidConfig();
        config.channels[0].gamma = value;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_day_start_not_before_end_is_rejected() {
    DeviceConfig config = makeValidConfig();
    config.profiles[0].dayStartMinute = 600;
    config.profiles[0].dayEndMinute = 600;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.profiles[0].dayStartMinute = 601;
    config.profiles[0].dayEndMinute = 600;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_day_minutes_out_of_range_are_rejected() {
    DeviceConfig config = makeValidConfig();
    config.profiles[0].dayStartMinute = MINUTES_PER_DAY;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.profiles[0].dayEndMinute = MINUTES_PER_DAY;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_stage_levels_invalid_values_are_rejected() {
    const float invalidValues[] = {
        -0.01f,
        100.01f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    for (float value : invalidValues) {
        DeviceConfig config = makeValidConfig();
        config.profiles[2].stages[4].levels.value[6] = value;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_night_levels_invalid_values_are_rejected() {
    const float invalidValues[] = {
        -0.01f,
        100.01f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    for (float value : invalidValues) {
        DeviceConfig config = makeValidConfig();
        config.profiles[3].nightLevels.value[7] = value;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_all_remaining_channel_floats_are_validated() {
    using Member = float ChannelConfig::*;
    const Member nonNegativeMembers[] = {
        &ChannelConfig::ledPowerW,
        &ChannelConfig::colorTemperatureK,
        &ChannelConfig::wavelengthNm
    };

    for (Member member : nonNegativeMembers) {
        DeviceConfig config = makeValidConfig();
        config.channels[0].*member = QUIET_NAN;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));

        config = makeValidConfig();
        config.channels[0].*member = POSITIVE_INFINITY;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));

        config = makeValidConfig();
        config.channels[0].*member = -0.01f;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }

    DeviceConfig config = makeValidConfig();
    config.channels[0].opticAngleDeg = QUIET_NAN;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    config.channels[0].opticAngleDeg = 180.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_all_tank_floats_are_validated() {
    using Member = float TankConfig::*;
    const Member members[] = {
        &TankConfig::volumeLiters,
        &TankConfig::tankLengthCm,
        &TankConfig::tankWidthCm,
        &TankConfig::tankHeightCm,
        &TankConfig::waterColumnHeightCm,
        &TankConfig::lampHeightAboveWaterCm,
        &TankConfig::lampLengthCm
    };

    for (Member member : members) {
        DeviceConfig config = makeValidConfig();
        config.tank.*member = QUIET_NAN;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));

        config = makeValidConfig();
        config.tank.*member = POSITIVE_INFINITY;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));

        config = makeValidConfig();
        config.tank.*member = -0.01f;
        TEST_ASSERT_FALSE(ConfigValidator::validate(config));
    }
}

void test_invalid_intensity_enum_is_rejected() {
    DeviceConfig config = makeValidConfig();
    config.tank.intensityLevel = static_cast<IntensityLevel>(255);

    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_unterminated_text_fields_are_rejected() {
    DeviceConfig config = makeValidConfig();
    std::memset(config.timezone, 'X', sizeof(config.timezone));
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    std::memset(
        config.channels[0].name,
        'X',
        sizeof(config.channels[0].name)
    );
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    std::memset(
        config.channels[0].ledModel,
        'X',
        sizeof(config.channels[0].ledModel)
    );
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    std::memset(
        config.channels[0].spectrumName,
        'X',
        sizeof(config.channels[0].spectrumName)
    );
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeValidConfig();
    std::memset(
        config.profiles[0].name,
        'X',
        sizeof(config.profiles[0].name)
    );
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

void test_requested_nan_gives_zero() {
    const DeviceConfig config = makeValidConfig();
    const float output = processChannel0(QUIET_NAN, config);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);
}

void test_requested_infinity_gives_zero() {
    const DeviceConfig config = makeValidConfig();

    float output = processChannel0(POSITIVE_INFINITY, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);

    output = processChannel0(-POSITIVE_INFINITY, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);
}

void test_light_engine_rejects_invalid_math_configuration() {
    DeviceConfig config = makeValidConfig();
    config.globalPowerLimitPercent = QUIET_NAN;
    float output = processChannel0(50.0f, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);

    config = makeValidConfig();
    config.channels[0].hardMaxPercent = POSITIVE_INFINITY;
    output = processChannel0(50.0f, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);

    config = makeValidConfig();
    config.channels[0].calibrationMinPercent = QUIET_NAN;
    output = processChannel0(50.0f, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);

    config = makeValidConfig();
    config.channels[0].calibrationMinPercent = 80.0f;
    config.channels[0].calibrationMaxPercent = 20.0f;
    output = processChannel0(50.0f, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);

    config = makeValidConfig();
    config.channels[0].gamma = QUIET_NAN;
    output = processChannel0(50.0f, config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, output);
    assertSafePercent(output);
}

void test_light_engine_outputs_are_always_finite_and_in_range() {
    const float requestedValues[] = {
        -1000.0f,
        -0.01f,
        0.0f,
        25.0f,
        100.0f,
        100.01f,
        1000.0f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    DeviceConfig config = makeValidConfig();
    config.channels[0].gamma = 2.2f;
    config.channels[0].calibrationMinPercent = 5.0f;
    config.channels[0].calibrationMaxPercent = 95.0f;

    for (float requested : requestedValues) {
        assertSafePercent(processChannel0(requested, config));
    }
}

void test_hard_max_is_absolute_post_calibration_limit() {
    DeviceConfig config = makeValidConfig();
    config.globalPowerLimitPercent = 100.0f;
    config.channels[0].hardMaxPercent = 70.0f;
    config.channels[0].gamma = 1.0f;
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        70.0f,
        processChannel0(100.0f, config)
    );
}

void test_lumacore_begin_rejects_invalid_configuration_safely() {
    DeviceConfig config = makeValidConfig();
    config.profiles[0].stages[0].levels.value[0] = QUIET_NAN;

    LumaCore core;
    TEST_ASSERT_FALSE(core.begin(config, 0U));

    LocalTime time {};
    time.valid = true;
    time.hour = 8;
    time.minute = 0;
    time.minuteOfDay = 480;

    core.update(time, STANDARD_TRANSITION_MS);

    TEST_ASSERT_FALSE(core.state().timeValid);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        0.0f,
        core.state().requestedLevels.value[0]
    );
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f,
        0.0f,
        core.state().actualLevels.value[0]
    );
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_valid_configuration_passes);
    RUN_TEST(test_profile_indexes_out_of_range_are_rejected);
    RUN_TEST(test_global_limit_invalid_values_are_rejected);
    RUN_TEST(test_hard_max_invalid_values_are_rejected);
    RUN_TEST(test_calibration_out_of_range_is_rejected);
    RUN_TEST(test_calibration_min_above_max_is_rejected);
    RUN_TEST(test_gamma_invalid_values_are_rejected);
    RUN_TEST(test_day_start_not_before_end_is_rejected);
    RUN_TEST(test_day_minutes_out_of_range_are_rejected);
    RUN_TEST(test_stage_levels_invalid_values_are_rejected);
    RUN_TEST(test_night_levels_invalid_values_are_rejected);
    RUN_TEST(test_all_remaining_channel_floats_are_validated);
    RUN_TEST(test_all_tank_floats_are_validated);
    RUN_TEST(test_invalid_intensity_enum_is_rejected);
    RUN_TEST(test_unterminated_text_fields_are_rejected);
    RUN_TEST(test_requested_nan_gives_zero);
    RUN_TEST(test_requested_infinity_gives_zero);
    RUN_TEST(test_light_engine_rejects_invalid_math_configuration);
    RUN_TEST(test_light_engine_outputs_are_always_finite_and_in_range);
    RUN_TEST(test_hard_max_is_absolute_post_calibration_limit);
    RUN_TEST(test_lumacore_begin_rejects_invalid_configuration_safely);

    UNITY_END();
}

void loop() {
}
