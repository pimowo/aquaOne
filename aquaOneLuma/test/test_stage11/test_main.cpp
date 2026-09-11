#include <Arduino.h>
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "Preferences.h"

#define LUMASENSE_STORAGE_PREFERENCES_HEADER     "../../test/test_stage11/Preferences.h"

#include "../../src/storage/ConfigValidator.cpp"
#include "../../src/storage/ConfigDefaults.cpp"
#include "../../src/storage/StorageService.cpp"

using namespace LumaSense;

namespace {

constexpr size_t HEADER_SIZE = 24U;
constexpr size_t MAGIC_OFFSET = 0U;
constexpr size_t SCHEMA_OFFSET = 6U;
constexpr size_t LENGTH_OFFSET = 8U;
constexpr size_t GENERATION_OFFSET = 12U;
constexpr size_t PAYLOAD_CRC_OFFSET = 16U;
constexpr size_t HEADER_CRC_OFFSET = 20U;
constexpr size_t HEADER_CRC_INPUT_SIZE = 20U;

constexpr const char* SLOT_A_KEY = "cfg_a";
constexpr const char* SLOT_B_KEY = "cfg_b";

uint32_t independentCrc32(
    const uint8_t* data,
    size_t length
) {
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];

        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U)
                ? (crc >> 1U) ^ 0xEDB88320UL
                : crc >> 1U;
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

void writeUint16(
    uint8_t* destination,
    uint16_t value
) {
    destination[0] =
        static_cast<uint8_t>(value);

    destination[1] =
        static_cast<uint8_t>(value >> 8U);
}

void writeUint32(
    uint8_t* destination,
    uint32_t value
) {
    destination[0] =
        static_cast<uint8_t>(value);

    destination[1] =
        static_cast<uint8_t>(value >> 8U);

    destination[2] =
        static_cast<uint8_t>(value >> 16U);

    destination[3] =
        static_cast<uint8_t>(value >> 24U);
}

uint32_t readUint32(
    const uint8_t* source
) {
    return
        static_cast<uint32_t>(source[0]) |
        (
            static_cast<uint32_t>(source[1])
            << 8U
        ) |
        (
            static_cast<uint32_t>(source[2])
            << 16U
        ) |
        (
            static_cast<uint32_t>(source[3])
            << 24U
        );
}

void updateHeaderCrc(
    const char* key
) {
    uint8_t* record =
        Preferences::raw(key);

    writeUint32(
        record + HEADER_CRC_OFFSET,
        independentCrc32(
            record,
            HEADER_CRC_INPUT_SIZE
        )
    );
}

void updatePayloadAndHeaderCrc(
    const char* key
) {
    uint8_t* record =
        Preferences::raw(key);

    writeUint32(
        record + PAYLOAD_CRC_OFFSET,
        independentCrc32(
            record + HEADER_SIZE,
            sizeof(DeviceConfig)
        )
    );

    updateHeaderCrc(key);
}

DeviceConfig configA {};
DeviceConfig configB {};
DeviceConfig configC {};

void fillConfig(
    DeviceConfig& config,
    float marker = 10.0f
) {
    config =
        createDefaultConfig();

    config.activeProfileIndex = 4;
    config.serviceProfileIndex = 3;
    config.globalPowerLimitPercent =
        80.0f + marker / 10.0f;

    std::snprintf(
        config.timezone,
        sizeof(config.timezone),
        "Europe/Warsaw"
    );

    config.tank.volumeLiters =
        100.0f + marker;

    config.tank.tankLengthCm = 80.0f;
    config.tank.tankWidthCm = 40.0f;
    config.tank.tankHeightCm = 50.0f;
    config.tank.waterColumnHeightCm = 42.0f;
    config.tank.lampHeightAboveWaterCm = 15.0f;
    config.tank.lampLengthCm = 75.0f;
    config.tank.intensityLevel = IntensityLevel::High;
    config.tank.co2Enabled = true;

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        ChannelConfig& item =
            config.channels[channel];

        item.enabled =
            (channel % 2U) == 0U;

        item.pwmInverted =
            (channel % 3U) == 0U;

        item.hardMaxPercent =
            70.0f + channel;

        item.calibrationMinPercent =
            1.0f + channel;

        item.calibrationMaxPercent =
            90.0f + channel;

        item.gamma =
            1.0f + channel * 0.1f;

        item.ledCount =
            static_cast<uint16_t>(
                10U + channel
            );

        item.ledPowerW =
            1.5f + channel * 0.1f;

        item.colorTemperatureK =
            5000.0f + channel * 100.0f;

        item.wavelengthNm =
            400.0f + channel * 10.0f;

        item.opticAngleDeg =
            90.0f + channel;

        std::snprintf(
            item.name,
            sizeof(item.name),
            "Channel %u",
            static_cast<unsigned>(
                channel + 1U
            )
        );

        std::snprintf(
            item.ledModel,
            sizeof(item.ledModel),
            "LED-%u",
            static_cast<unsigned>(
                channel
            )
        );

        std::snprintf(
            item.spectrumName,
            sizeof(item.spectrumName),
            "Spectrum-%u",
            static_cast<unsigned>(
                channel
            )
        );
    }

    for (
        uint8_t profile = 0;
        profile < PROFILE_COUNT;
        ++profile
    ) {
        Profile& item =
            config.profiles[profile];

        std::snprintf(
            item.name,
            sizeof(item.name),
            "Profile %u",
            static_cast<unsigned>(
                profile + 1U
            )
        );

        item.dayStartMinute =
            static_cast<uint16_t>(
                300U + profile * 10U
            );

        item.dayEndMinute =
            static_cast<uint16_t>(
                1100U + profile * 10U
            );

        item.nightEnabled =
            (profile % 2U) != 0U;

        item.generated =
            (profile % 2U) == 0U;

        item.generatorVersion =
            static_cast<uint16_t>(
                100U + profile
            );

        item.generatorOutdated =
            profile == 4U;

        for (
            uint8_t stage = 0;
            stage < DAY_STAGE_COUNT;
            ++stage
        ) {
            item.stages[stage].id =
                static_cast<DayStageId>(
                    stage
                );

            for (
                uint8_t channel = 0;
                channel < CHANNEL_COUNT;
                ++channel
            ) {
                item
                    .stages[stage]
                    .levels
                    .value[channel] =
                        static_cast<float>(
                            (
                                marker +
                                profile * 7U +
                                stage * 4U +
                                channel * 2U
                            )
                        );
            }
        }

        for (
            uint8_t channel = 0;
            channel < CHANNEL_COUNT;
            ++channel
        ) {
            item.nightLevels.value[channel] =
                marker / 10.0f +
                profile +
                channel * 0.25f;
        }
    }
}
 
void assertConfigEqual(
    const DeviceConfig& expected,
    const DeviceConfig& actual
) {
    TEST_ASSERT_EQUAL_MEMORY(
        &expected,
        &actual,
        sizeof(DeviceConfig)
    );
}

bool loadConfig(
    DeviceConfig& config
) {
    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    return storage.load(config);
}

void saveTwice(
    const DeviceConfig& first,
    const DeviceConfig& second
) {
    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(first));
    TEST_ASSERT_TRUE(storage.save(second));
}

void test_valid_config_save_and_load() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));
    TEST_ASSERT_TRUE(storage.hasValidConfig());

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_defaults_pass_validator() {
    configA =
        createDefaultConfig();

    TEST_ASSERT_EQUAL_UINT16(
        DEVICE_CONFIG_SCHEMA_VERSION,
        configA.schemaVersion
    );

    TEST_ASSERT_TRUE(
        ConfigValidator::validate(configA)
    );
}

void test_invalid_config_cannot_be_saved() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    configB = configA;
    configB.globalPowerLimitPercent =
        std::numeric_limits<float>::quiet_NaN();

    TEST_ASSERT_FALSE(storage.save(configB));

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_empty_storage_load_returns_false() {
    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_FALSE(storage.hasValidConfig());

    fillConfig(configA, 20.0f);
    configB = configA;

    TEST_ASSERT_FALSE(storage.load(configA));
    assertConfigEqual(configB, configA);
}

void test_one_valid_slot_is_loaded() {
    fillConfig(configA, 11.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    TEST_ASSERT_TRUE(
        Preferences::length(SLOT_A_KEY) > 0U
    );

    TEST_ASSERT_EQUAL_UINT32(
        0U,
        Preferences::length(SLOT_B_KEY)
    );

    configC = {};
    TEST_ASSERT_TRUE(loadConfig(configC));
    assertConfigEqual(configA, configC);
}

void test_corrupt_a_valid_b_loads_b() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);
    saveTwice(configA, configB);

    Preferences::raw(SLOT_A_KEY)[
        HEADER_SIZE + 10U
    ] ^= 0x01U;

    configC = {};
    TEST_ASSERT_TRUE(loadConfig(configC));
    assertConfigEqual(configB, configC);
}

void test_corrupt_b_valid_a_loads_a() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);
    saveTwice(configA, configB);

    Preferences::raw(SLOT_B_KEY)[
        HEADER_SIZE + 10U
    ] ^= 0x01U;

    configC = {};
    TEST_ASSERT_TRUE(loadConfig(configC));
    assertConfigEqual(configA, configC);
}

void test_both_valid_newer_generation_wins() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);
    saveTwice(configA, configB);

    configC = {};
    TEST_ASSERT_TRUE(loadConfig(configC));
    assertConfigEqual(configB, configC);
}

void test_payload_crc_mismatch_is_rejected() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    Preferences::raw(SLOT_A_KEY)[
        HEADER_SIZE + sizeof(DeviceConfig) / 2U
    ] ^= 0x80U;

    configC = {};
    TEST_ASSERT_FALSE(loadConfig(configC));
}

void test_bad_magic_is_rejected() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    writeUint32(
        Preferences::raw(SLOT_A_KEY) +
            MAGIC_OFFSET,
        0xDEADBEEFUL
    );

    updateHeaderCrc(SLOT_A_KEY);

    configC = {};
    TEST_ASSERT_FALSE(loadConfig(configC));
}

void test_bad_payload_length_is_rejected() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    writeUint32(
        Preferences::raw(SLOT_A_KEY) +
            LENGTH_OFFSET,
        static_cast<uint32_t>(
            sizeof(DeviceConfig) - 1U
        )
    );

    updateHeaderCrc(SLOT_A_KEY);

    configC = {};
    TEST_ASSERT_FALSE(loadConfig(configC));
}

void test_unsupported_schema_version_is_rejected() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    uint8_t* record =
        Preferences::raw(SLOT_A_KEY);

    writeUint16(
        record + SCHEMA_OFFSET,
        DEVICE_CONFIG_SCHEMA_VERSION + 1U
    );

    std::memcpy(
        &configB,
        record + HEADER_SIZE,
        sizeof(configB)
    );

    configB.schemaVersion =
        DEVICE_CONFIG_SCHEMA_VERSION + 1U;

    std::memcpy(
        record + HEADER_SIZE,
        &configB,
        sizeof(configB)
    );

    updatePayloadAndHeaderCrc(
        SLOT_A_KEY
    );

    configC = {};
    TEST_ASSERT_FALSE(loadConfig(configC));
}

void test_crc_valid_semantically_invalid_config_is_rejected() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    uint8_t* record =
        Preferences::raw(SLOT_A_KEY);

    std::memcpy(
        &configB,
        record + HEADER_SIZE,
        sizeof(configB)
    );

    configB.globalPowerLimitPercent =
        -1.0f;

    std::memcpy(
        record + HEADER_SIZE,
        &configB,
        sizeof(configB)
    );

    updatePayloadAndHeaderCrc(
        SLOT_A_KEY
    );

    configC = {};
    TEST_ASSERT_FALSE(loadConfig(configC));
}

void test_partial_new_slot_keeps_previous_config() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    Preferences::partialNextWrite(
        HEADER_SIZE + 16U
    );

    TEST_ASSERT_FALSE(
        storage.save(configB)
    );

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_write_error_keeps_previous_config() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    Preferences::failNextWrite();

    TEST_ASSERT_FALSE(
        storage.save(configB)
    );

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_post_write_verification_detects_corruption() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    Preferences::corruptAfterNextWrite(
        HEADER_SIZE + 5U
    );

    TEST_ASSERT_FALSE(
        storage.save(configB)
    );

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_next_save_switches_slot() {
    fillConfig(configA, 10.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    TEST_ASSERT_TRUE(
        Preferences::length(SLOT_A_KEY) > 0U
    );

    TEST_ASSERT_EQUAL_UINT32(
        0U,
        Preferences::length(SLOT_B_KEY)
    );

    fillConfig(configB, 20.0f);
    TEST_ASSERT_TRUE(storage.save(configB));

    TEST_ASSERT_TRUE(
        Preferences::length(SLOT_B_KEY) > 0U
    );

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configB, configC);
}

void test_multiple_saves_keep_latest_config() {
    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());

    for (uint8_t index = 0; index < 8U; ++index) {
        fillConfig(
            configA,
            static_cast<float>(
                10U + index
            )
        );

        TEST_ASSERT_TRUE(
            storage.save(configA)
        );
    }

    fillConfig(configB, 17.0f);

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configB, configC);
}

void test_generation_overflow_selects_wrapped_record() {
    fillConfig(configA, 10.0f);
    fillConfig(configB, 20.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    uint8_t* slotA =
        Preferences::raw(SLOT_A_KEY);

    writeUint32(
        slotA + GENERATION_OFFSET,
        UINT32_MAX
    );

    updateHeaderCrc(SLOT_A_KEY);

    TEST_ASSERT_TRUE(
        storage.save(configB)
    );

    TEST_ASSERT_EQUAL_UINT32(
        0U,
        readUint32(
            Preferences::raw(SLOT_B_KEY) +
                GENERATION_OFFSET
        )
    );

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configB, configC);
}

void test_texts_and_profiles_are_bitwise_preserved() {
    fillConfig(configA, 19.0f);

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    configC = {};
    TEST_ASSERT_TRUE(storage.load(configC));
    assertConfigEqual(configA, configC);
}

void test_save_does_not_modify_input() {
    fillConfig(configA, 13.0f);
    configB = configA;

    StorageService storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.save(configA));

    assertConfigEqual(configB, configA);
}

} // namespace

void setUp() {
    Preferences::reset();
}

void tearDown() {
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_valid_config_save_and_load);
    RUN_TEST(test_defaults_pass_validator);
    RUN_TEST(test_invalid_config_cannot_be_saved);
    RUN_TEST(test_empty_storage_load_returns_false);
    RUN_TEST(test_one_valid_slot_is_loaded);
    RUN_TEST(test_corrupt_a_valid_b_loads_b);
    RUN_TEST(test_corrupt_b_valid_a_loads_a);
    RUN_TEST(test_both_valid_newer_generation_wins);
    RUN_TEST(test_payload_crc_mismatch_is_rejected);
    RUN_TEST(test_bad_magic_is_rejected);
    RUN_TEST(test_bad_payload_length_is_rejected);
    RUN_TEST(test_unsupported_schema_version_is_rejected);
    RUN_TEST(test_crc_valid_semantically_invalid_config_is_rejected);
    RUN_TEST(test_partial_new_slot_keeps_previous_config);
    RUN_TEST(test_write_error_keeps_previous_config);
    RUN_TEST(test_post_write_verification_detects_corruption);
    RUN_TEST(test_next_save_switches_slot);
    RUN_TEST(test_multiple_saves_keep_latest_config);
    RUN_TEST(test_generation_overflow_selects_wrapped_record);
    RUN_TEST(test_texts_and_profiles_are_bitwise_preserved);
    RUN_TEST(test_save_does_not_modify_input);

    UNITY_END();
}

void loop() {
}