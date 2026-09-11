#include <Arduino.h>
#include <unity.h>

#include <cmath>
#include <limits>

#include "../../src/core/LightEngine.cpp"
#include "../../src/storage/ConfigValidator.cpp"

using namespace LumaSense;

namespace {

constexpr float EPSILON = 0.0005f;

const float QUIET_NAN =
    std::numeric_limits<float>::quiet_NaN();
const float POSITIVE_INFINITY =
    std::numeric_limits<float>::infinity();

DeviceConfig makeConfig() {
    DeviceConfig config {};
    config.globalPowerLimitPercent = 100.0f;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        config.profiles[profile].dayStartMinute = 480;
        config.profiles[profile].dayEndMinute = 1140;
    }

    ChannelConfig& channel = config.channels[0];
    channel.enabled = true;
    channel.gamma = 1.0f;
    channel.calibrationMinPercent = 0.0f;
    channel.calibrationMaxPercent = 100.0f;
    channel.hardMaxPercent = 100.0f;

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

void assertLevel(float expected, float actual) {
    TEST_ASSERT_TRUE(std::isfinite(actual));
    TEST_ASSERT_FLOAT_WITHIN(EPSILON, expected, actual);
}

void assertSafeLevel(float actual) {
    TEST_ASSERT_TRUE(std::isfinite(actual));
    TEST_ASSERT_TRUE(actual >= 0.0f);
    TEST_ASSERT_TRUE(actual <= 100.0f);
}

void test_zero_bypasses_positive_calibration_minimum() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;

    assertLevel(0.0f, processChannel0(0.0f, config));
}

void test_disabled_channel_is_zero() {
    DeviceConfig config = makeConfig();
    config.channels[0].enabled = false;

    assertLevel(0.0f, processChannel0(100.0f, config));
}

void test_global_limit_is_applied_before_gamma() {
    DeviceConfig config = makeConfig();
    config.globalPowerLimitPercent = 50.0f;
    config.channels[0].gamma = 2.0f;

    // 80% * 50% = 40%, then gamma 2.0 => 16%.
    assertLevel(16.0f, processChannel0(80.0f, config));
}

void test_zero_global_limit_bypasses_calibration_minimum() {
    DeviceConfig config = makeConfig();
    config.globalPowerLimitPercent = 0.0f;
    config.channels[0].calibrationMinPercent = 20.0f;

    assertLevel(0.0f, processChannel0(50.0f, config));
}

void test_gamma_one_with_full_calibration_range_is_identity() {
    DeviceConfig config = makeConfig();

    assertLevel(37.5f, processChannel0(37.5f, config));
}

void test_non_unity_gamma_is_applied() {
    DeviceConfig config = makeConfig();
    config.channels[0].gamma = 2.0f;

    assertLevel(25.0f, processChannel0(50.0f, config));
}

void test_positive_calibration_minimum_maps_positive_signal() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 80.0f;

    assertLevel(35.0f, processChannel0(25.0f, config));
}

void test_calibration_maximum_below_100_limits_mapping() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMaxPercent = 60.0f;

    assertLevel(60.0f, processChannel0(100.0f, config));
}

void test_hard_max_below_calibration_max_is_absolute_limit() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMaxPercent = 90.0f;
    config.channels[0].hardMaxPercent = 70.0f;

    assertLevel(70.0f, processChannel0(100.0f, config));
}

void test_hard_max_above_calibration_max_does_not_change_result() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMaxPercent = 70.0f;
    config.channels[0].hardMaxPercent = 90.0f;

    assertLevel(70.0f, processChannel0(100.0f, config));
}

void test_hard_max_zero_forces_zero() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].hardMaxPercent = 0.0f;

    assertLevel(0.0f, processChannel0(100.0f, config));
}

void test_hard_max_100_adds_no_limit() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;
    config.channels[0].hardMaxPercent = 100.0f;

    assertLevel(90.0f, processChannel0(100.0f, config));
}

void test_full_request_is_capped_at_70() {
    DeviceConfig config = makeConfig();
    config.channels[0].hardMaxPercent = 70.0f;

    assertLevel(70.0f, processChannel0(100.0f, config));
}

void test_calibrated_full_request_is_capped_at_70() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;
    config.channels[0].hardMaxPercent = 70.0f;

    assertLevel(70.0f, processChannel0(100.0f, config));
}

void test_small_positive_request_reaches_calibration_minimum() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;

    const float output = processChannel0(0.01f, config);

    assertSafeLevel(output);
    TEST_ASSERT_TRUE(output >= 20.0f);
}

void test_hard_max_below_calibration_minimum_wins() {
    DeviceConfig config = makeConfig();
    config.channels[0].calibrationMinPercent = 20.0f;
    config.channels[0].calibrationMaxPercent = 90.0f;
    config.channels[0].hardMaxPercent = 10.0f;

    assertLevel(10.0f, processChannel0(0.01f, config));
}

void test_nan_and_infinity_still_return_safe_zero() {
    DeviceConfig config = makeConfig();
    assertLevel(0.0f, processChannel0(QUIET_NAN, config));
    assertLevel(0.0f, processChannel0(POSITIVE_INFINITY, config));
    assertLevel(0.0f, processChannel0(-POSITIVE_INFINITY, config));

    config = makeConfig();
    config.globalPowerLimitPercent = QUIET_NAN;
    assertLevel(0.0f, processChannel0(50.0f, config));

    config = makeConfig();
    config.channels[0].gamma = POSITIVE_INFINITY;
    assertLevel(0.0f, processChannel0(50.0f, config));

    config = makeConfig();
    config.channels[0].calibrationMaxPercent = QUIET_NAN;
    assertLevel(0.0f, processChannel0(50.0f, config));

    config = makeConfig();
    config.channels[0].hardMaxPercent = POSITIVE_INFINITY;
    assertLevel(0.0f, processChannel0(50.0f, config));
}

void test_final_output_is_always_finite_and_in_range() {
    const float requestedValues[] = {
        -1000.0f,
        -0.01f,
        0.0f,
        0.01f,
        25.0f,
        100.0f,
        100.01f,
        1000.0f,
        QUIET_NAN,
        POSITIVE_INFINITY,
        -POSITIVE_INFINITY
    };

    DeviceConfig config = makeConfig();
    config.globalPowerLimitPercent = 73.0f;
    config.channels[0].gamma = 2.2f;
    config.channels[0].calibrationMinPercent = 12.0f;
    config.channels[0].calibrationMaxPercent = 96.0f;
    config.channels[0].hardMaxPercent = 67.0f;

    for (float requested : requestedValues) {
        assertSafeLevel(processChannel0(requested, config));
    }
}

void test_validator_hard_max_calibration_and_gamma_contract_is_unchanged() {
    DeviceConfig config = makeConfig();
    TEST_ASSERT_TRUE(ConfigValidator::validate(config));

    config.channels[0].hardMaxPercent = 100.01f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeConfig();
    config.channels[0].calibrationMinPercent = 80.0f;
    config.channels[0].calibrationMaxPercent = 20.0f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));

    config = makeConfig();
    config.channels[0].gamma = 0.0f;
    TEST_ASSERT_FALSE(ConfigValidator::validate(config));
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_zero_bypasses_positive_calibration_minimum);
    RUN_TEST(test_disabled_channel_is_zero);
    RUN_TEST(test_global_limit_is_applied_before_gamma);
    RUN_TEST(test_zero_global_limit_bypasses_calibration_minimum);
    RUN_TEST(test_gamma_one_with_full_calibration_range_is_identity);
    RUN_TEST(test_non_unity_gamma_is_applied);
    RUN_TEST(test_positive_calibration_minimum_maps_positive_signal);
    RUN_TEST(test_calibration_maximum_below_100_limits_mapping);
    RUN_TEST(test_hard_max_below_calibration_max_is_absolute_limit);
    RUN_TEST(test_hard_max_above_calibration_max_does_not_change_result);
    RUN_TEST(test_hard_max_zero_forces_zero);
    RUN_TEST(test_hard_max_100_adds_no_limit);
    RUN_TEST(test_full_request_is_capped_at_70);
    RUN_TEST(test_calibrated_full_request_is_capped_at_70);
    RUN_TEST(test_small_positive_request_reaches_calibration_minimum);
    RUN_TEST(test_hard_max_below_calibration_minimum_wins);
    RUN_TEST(test_nan_and_infinity_still_return_safe_zero);
    RUN_TEST(test_final_output_is_always_finite_and_in_range);
    RUN_TEST(test_validator_hard_max_calibration_and_gamma_contract_is_unchanged);

    UNITY_END();
}

void loop() {
}
