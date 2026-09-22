#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <type_traits>

#include <unity.h>

#include "AquaCore/Commands/DomainCommandResult.h"

namespace {

using AquaCore::Commands::DomainCommandResult;

static_assert(
    std::is_enum<DomainCommandResult>::value,
    "DomainCommandResult must be an enum"
);
static_assert(
    std::is_trivially_copyable<DomainCommandResult>::value,
    "DomainCommandResult must be trivially copyable"
);
static_assert(
    !std::is_convertible<DomainCommandResult, uint8_t>::value,
    "DomainCommandResult must remain a scoped enum"
);

void test_all_domain_command_results_are_distinct() {
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::Completed),
        static_cast<uint8_t>(DomainCommandResult::Rejected)
    );
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::Rejected),
        static_cast<uint8_t>(DomainCommandResult::InvalidState)
    );
    TEST_ASSERT_NOT_EQUAL(
        static_cast<uint8_t>(DomainCommandResult::InvalidState),
        static_cast<uint8_t>(DomainCommandResult::OperationStarted)
    );
}

void test_domain_command_result_copy_preserves_value() {
    const DomainCommandResult original =
        DomainCommandResult::OperationStarted;
    const DomainCommandResult copied = original;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(original),
        static_cast<uint8_t>(copied)
    );
}

void test_domain_command_result_names_are_stable() {
    TEST_ASSERT_EQUAL_STRING(
        "COMPLETED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::Completed
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "REJECTED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::Rejected
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "INVALID_STATE",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::InvalidState
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "OPERATION_STARTED",
        AquaCore::Commands::domainCommandResultName(
            DomainCommandResult::OperationStarted
        )
    );
}

void test_unknown_domain_command_result_has_fallback_name() {
    TEST_ASSERT_EQUAL_STRING(
        "UNKNOWN",
        AquaCore::Commands::domainCommandResultName(
            static_cast<DomainCommandResult>(0xFFU)
        )
    );
}

void runTests() {
    UNITY_BEGIN();
    RUN_TEST(test_all_domain_command_results_are_distinct);
    RUN_TEST(test_domain_command_result_copy_preserves_value);
    RUN_TEST(test_domain_command_result_names_are_stable);
    RUN_TEST(test_unknown_domain_command_result_has_fallback_name);
}

} // namespace

#if defined(ARDUINO)

void setup() {
    delay(2000);
    runTests();
    UNITY_END();
}

void loop() {
}

#else

int main() {
    runTests();
    return UNITY_END();
}

#endif
