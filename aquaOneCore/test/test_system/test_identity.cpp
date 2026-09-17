#include <unity.h>
#include <string.h>
#include <type_traits>

#include "AquaCore/System/Identity.h"
#include "AquaCore/Version.h"

namespace Id = AquaCore::Identity;

static_assert(!std::is_same<Id::DeviceIdentity, AquaCore::DeviceIdentity>::value,
    "Neutral identity must not replace legacy identity");
static_assert(!std::is_same<Id::DeviceIdentity, Id::BuildIdentity>::value,
    "Device and Build identity must be separate types");
static_assert(
    !std::is_default_constructible<Id::ValidationResult>::value,
    "ValidationResult must not be default constructible"
);
static_assert(!std::is_same<Id::HardwareIdentity, Id::BuildIdentity>::value,
    "Hardware and Build identity must be separate types");

namespace {

void assertError(Id::ValidationResult result, Id::ValidationError error,
    Id::ValidationField field) {
    TEST_ASSERT_FALSE(result.isValid());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(error), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(field), static_cast<int>(result.field));
}

template <size_t Capacity>
void fillText(char (&text)[Capacity], size_t length) {
    memset(text, 'x', length);
    text[length] = '\0';
}

void test_device_identity_valid() {
    Id::DeviceIdentity identity;
    const Id::ValidationResult result = identity.assign("luma");
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Id::ValidationField::None), static_cast<int>(result.field));
    TEST_ASSERT_TRUE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("luma", identity.deviceType());
}

void test_device_identity_null() {
    Id::DeviceIdentity identity;
    TEST_ASSERT_TRUE(identity.assign("luma").isValid());
    assertError(identity.assign(nullptr), Id::ValidationError::NullInput, Id::ValidationField::DeviceType);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.deviceType());
}

void test_device_identity_empty() {
    Id::DeviceIdentity identity;
    assertError(identity.assign(""), Id::ValidationError::EmptyRequiredField, Id::ValidationField::DeviceType);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.deviceType());
}

void test_device_identity_capacity_boundary() {
    Id::DeviceIdentity identity;
    char input[Id::DeviceIdentity::DEVICE_TYPE_CAPACITY] {};
    fillText(input, sizeof(input) - 1U);
    TEST_ASSERT_TRUE(identity.assign(input).isValid());
    TEST_ASSERT_EQUAL_STRING(input, identity.deviceType());
}

void test_device_identity_too_long() {
    Id::DeviceIdentity identity;
    char input[Id::DeviceIdentity::DEVICE_TYPE_CAPACITY + 1U] {};
    fillText(input, sizeof(input) - 1U);
    TEST_ASSERT_TRUE(identity.assign("luma").isValid());
    assertError(identity.assign(input), Id::ValidationError::TooLong, Id::ValidationField::DeviceType);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.deviceType());
    TEST_ASSERT_EQUAL_UINT('x', input[sizeof(input) - 2U]);
}

void test_hardware_identity_valid() {
    Id::HardwareIdentity identity;
    const Id::ValidationResult result = identity.assign("LOLIN32_TEST");
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Id::ValidationField::None), static_cast<int>(result.field));
    TEST_ASSERT_TRUE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("LOLIN32_TEST", identity.hardwareVariant());
}

void test_hardware_identity_null() {
    Id::HardwareIdentity identity;
    TEST_ASSERT_TRUE(identity.assign("LOLIN32_TEST").isValid());
    assertError(identity.assign(nullptr), Id::ValidationError::NullInput, Id::ValidationField::HardwareVariant);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.hardwareVariant());
}

void test_hardware_identity_empty() {
    Id::HardwareIdentity identity;
    assertError(identity.assign(""), Id::ValidationError::EmptyRequiredField, Id::ValidationField::HardwareVariant);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.hardwareVariant());
}

void test_hardware_identity_capacity_boundary() {
    Id::HardwareIdentity identity;
    char input[Id::HardwareIdentity::HARDWARE_VARIANT_CAPACITY] {};
    fillText(input, sizeof(input) - 1U);
    TEST_ASSERT_TRUE(identity.assign(input).isValid());
    TEST_ASSERT_EQUAL_STRING(input, identity.hardwareVariant());
}

void test_hardware_identity_too_long() {
    Id::HardwareIdentity identity;
    char input[Id::HardwareIdentity::HARDWARE_VARIANT_CAPACITY + 1U] {};
    fillText(input, sizeof(input) - 1U);
    TEST_ASSERT_TRUE(identity.assign("LOLIN32_TEST").isValid());
    assertError(identity.assign(input), Id::ValidationError::TooLong, Id::ValidationField::HardwareVariant);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.hardwareVariant());
    TEST_ASSERT_EQUAL_UINT('x', input[sizeof(input) - 2U]);
}

void test_build_identity_valid() {
    Id::BuildIdentity identity;
    const Id::ValidationResult result = identity.assign("0.2.1", AQUA_CORE_VERSION);
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_TRUE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("0.2.1", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING(AQUA_CORE_VERSION, identity.coreVersion());
}

void test_build_identity_null() {
    Id::BuildIdentity identity;
    TEST_ASSERT_TRUE(identity.assign("0.2.1", AQUA_CORE_VERSION).isValid());
    assertError(identity.assign(nullptr, AQUA_CORE_VERSION), Id::ValidationError::NullInput, Id::ValidationField::FirmwareVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", identity.coreVersion());
    TEST_ASSERT_TRUE(identity.assign("0.2.1", AQUA_CORE_VERSION).isValid());
    assertError(identity.assign("0.2.1", nullptr), Id::ValidationError::NullInput, Id::ValidationField::CoreVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", identity.coreVersion());
}

void test_build_identity_empty() {
    Id::BuildIdentity identity;
    assertError(identity.assign("", AQUA_CORE_VERSION), Id::ValidationError::EmptyRequiredField, Id::ValidationField::FirmwareVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    assertError(identity.assign("0.2.1", ""), Id::ValidationError::EmptyRequiredField, Id::ValidationField::CoreVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", identity.coreVersion());
}

void test_build_identity_capacity_boundary() {
    Id::BuildIdentity identity;
    char firmware[Id::BuildIdentity::FIRMWARE_VERSION_CAPACITY] {};
    char core[Id::BuildIdentity::CORE_VERSION_CAPACITY] {};
    fillText(firmware, sizeof(firmware) - 1U);
    fillText(core, sizeof(core) - 1U);
    TEST_ASSERT_TRUE(identity.assign(firmware, core).isValid());
    TEST_ASSERT_EQUAL_STRING(firmware, identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING(core, identity.coreVersion());
}

void test_build_identity_too_long() {
    Id::BuildIdentity identity;
    char firmware[Id::BuildIdentity::FIRMWARE_VERSION_CAPACITY + 1U] {};
    char core[Id::BuildIdentity::CORE_VERSION_CAPACITY + 1U] {};
    fillText(firmware, sizeof(firmware) - 1U);
    fillText(core, sizeof(core) - 1U);
    assertError(identity.assign(firmware, AQUA_CORE_VERSION), Id::ValidationError::TooLong, Id::ValidationField::FirmwareVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", identity.coreVersion());
    TEST_ASSERT_TRUE(identity.assign("0.2.1", AQUA_CORE_VERSION).isValid());
    assertError(identity.assign("0.2.1", core), Id::ValidationError::TooLong, Id::ValidationField::CoreVersion);
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_STRING("", identity.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", identity.coreVersion());
}

void test_identity_defaults_are_invalid() {
    Id::DeviceIdentity device;
    Id::BuildIdentity build;
    Id::HardwareIdentity hardware;
    TEST_ASSERT_FALSE(device.isValid());
    TEST_ASSERT_FALSE(build.isValid());
    TEST_ASSERT_FALSE(hardware.isValid());
    TEST_ASSERT_EQUAL_STRING("", device.deviceType());
    TEST_ASSERT_EQUAL_STRING("", build.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("", build.coreVersion());
    TEST_ASSERT_EQUAL_STRING("", hardware.hardwareVariant());
}

void test_identity_copies_own_storage() {
    char deviceInput[] = "luma";
    char firmwareInput[] = "0.2.1";
    char hardwareInput[] = "LOLIN32_TEST";
    Id::DeviceIdentity device;
    Id::BuildIdentity build;
    Id::HardwareIdentity hardware;
    TEST_ASSERT_TRUE(device.assign(deviceInput).isValid());
    TEST_ASSERT_TRUE(build.assign(firmwareInput, AQUA_CORE_VERSION).isValid());
    TEST_ASSERT_TRUE(hardware.assign(hardwareInput).isValid());
    const Id::DeviceIdentity deviceCopy(device);
    Id::BuildIdentity buildCopy;
    buildCopy = build;
    const Id::HardwareIdentity hardwareCopy(hardware);
    TEST_ASSERT_EQUAL_STRING(deviceInput, device.deviceType());
    TEST_ASSERT_EQUAL_STRING(firmwareInput, build.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING(hardwareInput, hardware.hardwareVariant());
    deviceInput[0] = 'X';
    firmwareInput[0] = 'X';
    hardwareInput[0] = 'X';
    TEST_ASSERT_TRUE(device.assign("hydro").isValid());
    TEST_ASSERT_TRUE(build.assign("0.3.0", "0.7.0").isValid());
    TEST_ASSERT_TRUE(hardware.assign("AQMA").isValid());
    TEST_ASSERT_TRUE(deviceCopy.isValid());
    TEST_ASSERT_TRUE(buildCopy.isValid());
    TEST_ASSERT_TRUE(hardwareCopy.isValid());
    TEST_ASSERT_EQUAL_STRING("luma", deviceCopy.deviceType());
    TEST_ASSERT_EQUAL_STRING("0.2.1", buildCopy.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING(AQUA_CORE_VERSION, buildCopy.coreVersion());
    TEST_ASSERT_EQUAL_STRING("LOLIN32_TEST", hardwareCopy.hardwareVariant());
}

void test_identity_categories_are_independent() {
    Id::DeviceIdentity device;
    Id::BuildIdentity build;
    Id::HardwareIdentity hardware;
    TEST_ASSERT_TRUE(device.assign("luma").isValid());
    TEST_ASSERT_TRUE(build.assign("0.2.1", AQUA_CORE_VERSION).isValid());
    TEST_ASSERT_TRUE(hardware.assign("LOLIN32_TEST").isValid());
    assertError(device.assign(nullptr), Id::ValidationError::NullInput, Id::ValidationField::DeviceType);
    TEST_ASSERT_TRUE(build.isValid());
    TEST_ASSERT_TRUE(hardware.isValid());
    TEST_ASSERT_EQUAL_STRING("0.2.1", build.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("LOLIN32_TEST", hardware.hardwareVariant());
}

void test_identity_assign_accepts_own_getters() {
    Id::DeviceIdentity device;
    Id::BuildIdentity build;
    Id::HardwareIdentity hardware;
    TEST_ASSERT_TRUE(device.assign("luma").isValid());
    TEST_ASSERT_TRUE(build.assign("0.2.1", AQUA_CORE_VERSION).isValid());
    TEST_ASSERT_TRUE(hardware.assign("LOLIN32_TEST").isValid());
    TEST_ASSERT_TRUE(device.assign(device.deviceType()).isValid());
    TEST_ASSERT_TRUE(build.assign(build.coreVersion(), build.firmwareVersion()).isValid());
    TEST_ASSERT_TRUE(hardware.assign(hardware.hardwareVariant()).isValid());
    TEST_ASSERT_EQUAL_STRING("luma", device.deviceType());
    TEST_ASSERT_EQUAL_STRING(AQUA_CORE_VERSION, build.firmwareVersion());
    TEST_ASSERT_EQUAL_STRING("0.2.1", build.coreVersion());
    TEST_ASSERT_EQUAL_STRING("LOLIN32_TEST", hardware.hardwareVariant());
}

} // namespace

void runIdentityTests() {
    RUN_TEST(test_device_identity_valid);
    RUN_TEST(test_device_identity_null);
    RUN_TEST(test_device_identity_empty);
    RUN_TEST(test_device_identity_capacity_boundary);
    RUN_TEST(test_device_identity_too_long);
    RUN_TEST(test_hardware_identity_valid);
    RUN_TEST(test_hardware_identity_null);
    RUN_TEST(test_hardware_identity_empty);
    RUN_TEST(test_hardware_identity_capacity_boundary);
    RUN_TEST(test_hardware_identity_too_long);
    RUN_TEST(test_build_identity_valid);
    RUN_TEST(test_build_identity_null);
    RUN_TEST(test_build_identity_empty);
    RUN_TEST(test_build_identity_capacity_boundary);
    RUN_TEST(test_build_identity_too_long);
    RUN_TEST(test_identity_defaults_are_invalid);
    RUN_TEST(test_identity_copies_own_storage);
    RUN_TEST(test_identity_categories_are_independent);
    RUN_TEST(test_identity_assign_accepts_own_getters);
}
