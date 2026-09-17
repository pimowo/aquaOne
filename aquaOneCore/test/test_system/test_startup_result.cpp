#include <type_traits>

#include <unity.h>

#include "AquaCore/System/Startup.h"

namespace {

namespace System = AquaCore::System;

static_assert(!std::is_default_constructible<System::StartupErrorCode>::value,
    "StartupErrorCode must not be default constructible");
static_assert(!std::is_default_constructible<System::StartupStepResult>::value,
    "StartupStepResult must not be default constructible");

void assertCode(const System::StartupErrorCode& actual,
    const char* ownerNamespace, const char* localCode) {
    TEST_ASSERT_TRUE(actual.isValid());
    TEST_ASSERT_EQUAL_STRING(ownerNamespace, actual.ownerNamespace());
    TEST_ASSERT_EQUAL_STRING(localCode, actual.localCode());
}

void testStartupErrorCodeAcceptsCoreAndDomainCodes() {
    assertCode(System::CoreStartupError::participantStorageInvalid(),
        "AQUA.CORE", "PARTICIPANT_STORAGE_INVALID");
    assertCode(System::StartupErrorCode::fromStatic(
        "AQUA.DOSER", "CONFIG_INVALID"),
        "AQUA.DOSER", "CONFIG_INVALID");
}

void testStartupErrorCodeRejectsNullAndEmptyTokens() {
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic(nullptr, "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", nullptr).isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", "").isValid());
}

void testStartupErrorCodeRejectsInvalidLocalGrammar() {
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", "lowercase").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", "HAS SPACE").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", "HAS:COLON").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.TEST", "1FIRST").isValid());
}

void testStartupErrorCodeRejectsInvalidNamespaceSegments() {
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("aqua.TEST", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA..TEST", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic(".AQUA", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA.1TEST", "ERROR").isValid());
    TEST_ASSERT_FALSE(System::StartupErrorCode::fromStatic("AQUA:TEST", "ERROR").isValid());
}

void testStartupErrorCodeEqualityUsesTextNotPointers() {
    static const char firstNamespace[] = "AQUA.TEST";
    static const char secondNamespace[] = "AQUA.TEST";
    static const char firstLocal[] = "SENSOR_FAILED";
    static const char secondLocal[] = "SENSOR_FAILED";
    const System::StartupErrorCode first =
        System::StartupErrorCode::fromStatic(firstNamespace, firstLocal);
    const System::StartupErrorCode second =
        System::StartupErrorCode::fromStatic(secondNamespace, secondLocal);
    TEST_ASSERT_NOT_EQUAL(firstNamespace, secondNamespace);
    TEST_ASSERT_NOT_EQUAL(firstLocal, secondLocal);
    TEST_ASSERT_TRUE(first.equals(second));
}

void testStartupErrorCodeInequalityUsesBothTokens() {
    const System::StartupErrorCode base =
        System::StartupErrorCode::fromStatic("AQUA.TEST", "FAILED");
    TEST_ASSERT_FALSE(base.equals(
        System::StartupErrorCode::fromStatic("AQUA.OTHER", "FAILED")));
    TEST_ASSERT_FALSE(base.equals(
        System::StartupErrorCode::fromStatic("AQUA.TEST", "OTHER")));
}

void testStartupStepResultSucceededHasNoError() {
    const System::StartupStepResult result = System::StartupStepResult::succeeded();
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::StartupOutcome::SUCCEEDED),
        static_cast<int>(result.outcome()));
    TEST_ASSERT_FALSE(result.hasErrorCode());
    TEST_ASSERT_NULL(result.errorCode());
}

void testStartupStepResultDisabledHasNoError() {
    const System::StartupStepResult result = System::StartupStepResult::disabled();
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::StartupOutcome::DISABLED),
        static_cast<int>(result.outcome()));
    TEST_ASSERT_FALSE(result.hasErrorCode());
    TEST_ASSERT_NULL(result.errorCode());
}

void testStartupStepResultFailedPreservesValidError() {
    const System::StartupErrorCode expected =
        System::StartupErrorCode::fromStatic("AQUA.TEST", "FAILED");
    const System::StartupStepResult result = System::StartupStepResult::failed(expected);
    TEST_ASSERT_TRUE(result.isValid());
    TEST_ASSERT_TRUE(result.hasErrorCode());
    TEST_ASSERT_NOT_NULL(result.errorCode());
    TEST_ASSERT_TRUE(result.errorCode()->equals(expected));
}

void testStartupStepResultFailedWithInvalidErrorIsInvalid() {
    const System::StartupStepResult missing = System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic(nullptr, nullptr));
    const System::StartupStepResult malformed = System::StartupStepResult::failed(
        System::StartupErrorCode::fromStatic("AQUA.test", "FAILED"));
    TEST_ASSERT_FALSE(missing.isValid());
    TEST_ASSERT_FALSE(missing.hasErrorCode());
    TEST_ASSERT_NULL(missing.errorCode());
    TEST_ASSERT_FALSE(malformed.isValid());
    TEST_ASSERT_TRUE(malformed.hasErrorCode());
    TEST_ASSERT_NOT_NULL(malformed.errorCode());
}

} // namespace

void runStartupResultTests() {
    RUN_TEST(testStartupErrorCodeAcceptsCoreAndDomainCodes);
    RUN_TEST(testStartupErrorCodeRejectsNullAndEmptyTokens);
    RUN_TEST(testStartupErrorCodeRejectsInvalidLocalGrammar);
    RUN_TEST(testStartupErrorCodeRejectsInvalidNamespaceSegments);
    RUN_TEST(testStartupErrorCodeEqualityUsesTextNotPointers);
    RUN_TEST(testStartupErrorCodeInequalityUsesBothTokens);
    RUN_TEST(testStartupStepResultSucceededHasNoError);
    RUN_TEST(testStartupStepResultDisabledHasNoError);
    RUN_TEST(testStartupStepResultFailedPreservesValidError);
    RUN_TEST(testStartupStepResultFailedWithInvalidErrorIsInvalid);
}
