#include <stdint.h>
#include <string.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/System/ApplicationRuntime.h"
#include "AquaCore/System/RuntimeIdentityStartup.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "AquaCore/System/Esp32RuntimeIdentityGenerator.h"
#endif

namespace {

namespace Id = AquaCore::Identity;
namespace System = AquaCore::System;

static_assert(std::is_copy_constructible<Id::RuntimeIdentity>::value,
    "RuntimeIdentity must be copyable");
static_assert(!std::is_copy_constructible<Id::RuntimeIdentityState>::value,
    "Authoritative runtime state must not be copied");
static_assert(!std::has_virtual_destructor<Id::RuntimeIdentityGenerator>::value,
    "Borrowed generator is not deleted through its interface");

class FakeGenerator final : public Id::RuntimeIdentityGenerator {
public:
    uint64_t values[3] = {1U, 2U, 3U};
    unsigned calls = 0U;
    bool fail = false;
    bool malformedSuccess = false;

    bool generate(Id::RuntimeIdentity& output) override {
        const unsigned index = calls++;
        if (fail) {
            return false;
        }
        if (malformedSuccess) {
            Id::RuntimeIdentity::fromValue(0U, output);
            return true;
        }
        return Id::RuntimeIdentity::fromValue(values[index < 3U ? index : 2U], output);
    }
};

Id::RuntimeIdentity identityFrom(uint64_t value) {
    Id::RuntimeIdentity identity;
    TEST_ASSERT_TRUE(Id::RuntimeIdentity::fromValue(value, identity));
    return identity;
}

void testDefaultAndZeroAreUnavailable() {
    Id::RuntimeIdentity identity;
    TEST_ASSERT_FALSE(identity.isValid());
    TEST_ASSERT_EQUAL_UINT64(0U, identity.value());
    TEST_ASSERT_FALSE(Id::RuntimeIdentity::fromValue(0U, identity));
    TEST_ASSERT_FALSE(identity.isValid());
}

void testNonzeroFactoryCopyAndFullWidthEquality() {
    const Id::RuntimeIdentity first = identityFrom(UINT64_C(0x1234567800000001));
    const Id::RuntimeIdentity copy = first;
    const Id::RuntimeIdentity different = identityFrom(UINT64_C(0x1234567900000001));
    TEST_ASSERT_TRUE(first.isValid());
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x1234567800000001), first.value());
    TEST_ASSERT_TRUE(first == copy);
    TEST_ASSERT_FALSE(first != copy);
    TEST_ASSERT_FALSE(first == different);
    TEST_ASSERT_TRUE(first != different);
    TEST_ASSERT_TRUE(Id::RuntimeIdentity() == Id::RuntimeIdentity());
}

void testCanonicalFormatPreservesLeadingZeros() {
    const Id::RuntimeIdentity identity = identityFrom(UINT64_C(0x1A2B));
    char output[Id::RuntimeIdentity::TEXT_CAPACITY] {};
    TEST_ASSERT_TRUE(identity.format(output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("0000000000001A2B", output);
    TEST_ASSERT_EQUAL_UINT(16U, strlen(output));
}

void testCanonicalFormatOfMaximumValue() {
    const Id::RuntimeIdentity identity = identityFrom(UINT64_MAX);
    char output[Id::RuntimeIdentity::TEXT_CAPACITY] {};
    TEST_ASSERT_TRUE(identity.format(output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("FFFFFFFFFFFFFFFF", output);
}

void testFormatterRejectsInvalidAndSmallStorageWithoutWriting() {
    const Id::RuntimeIdentity invalid;
    const Id::RuntimeIdentity valid = identityFrom(1U);
    char output[Id::RuntimeIdentity::TEXT_CAPACITY] = "unchanged";
    TEST_ASSERT_FALSE(invalid.format(output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("unchanged", output);
    TEST_ASSERT_FALSE(valid.format(output, 16U));
    TEST_ASSERT_EQUAL_STRING("unchanged", output);
    TEST_ASSERT_FALSE(valid.format(nullptr, sizeof(output)));
    TEST_ASSERT_TRUE(valid.format(output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("0000000000000001", output);
}

void testOneShotSuccessAndStableRead() {
    FakeGenerator generator;
    generator.values[0] = UINT64_C(0xFEDCBA9876543210);
    Id::RuntimeIdentityState state;
    TEST_ASSERT_TRUE(state.initialize(generator));
    TEST_ASSERT_TRUE(state.hasAttempted());
    TEST_ASSERT_TRUE(state.initialize(generator));
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_EQUAL_UINT64(generator.values[0], state.identity().value());
    const Id::RuntimeIdentity copy = state.identity();
    TEST_ASSERT_TRUE(copy == state.identity());
}

void testFailureIsOneShotAndUnavailable() {
    FakeGenerator generator;
    generator.fail = true;
    Id::RuntimeIdentityState state;
    TEST_ASSERT_FALSE(state.initialize(generator));
    generator.fail = false;
    TEST_ASSERT_FALSE(state.initialize(generator));
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_TRUE(state.hasAttempted());
    TEST_ASSERT_FALSE(state.identity().isValid());
}

void testMalformedGeneratorSuccessIsRejected() {
    FakeGenerator generator;
    generator.malformedSuccess = true;
    Id::RuntimeIdentityState state;
    TEST_ASSERT_FALSE(state.initialize(generator));
    TEST_ASSERT_FALSE(state.identity().isValid());
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
}

void testFreshOwnerInvokesGeneratorAgain() {
    FakeGenerator generator;
    generator.values[0] = 7U;
    generator.values[1] = 7U; // A fresh attempt need not yield a distinct value.
    Id::RuntimeIdentityState first;
    Id::RuntimeIdentityState second;
    TEST_ASSERT_TRUE(first.initialize(generator));
    TEST_ASSERT_TRUE(second.initialize(generator));
    TEST_ASSERT_EQUAL_UINT(2U, generator.calls);
    TEST_ASSERT_TRUE(first.identity().isValid());
    TEST_ASSERT_TRUE(second.identity().isValid());
}

struct StepContext {
    Id::RuntimeIdentityState* state;
    bool expectAvailable;
    bool fail;
    unsigned calls;
};

System::StartupStepResult step(void* raw) {
    StepContext* context = static_cast<StepContext*>(raw);
    ++context->calls;
    if (context->state->identity().isValid() != context->expectAvailable) {
        return System::StartupStepResult::failed(
            System::StartupErrorCode::fromStatic("AQUA.TEST", "ORDER_INVALID")
        );
    }
    return context->fail
        ? System::StartupStepResult::failed(
            System::StartupErrorCode::fromStatic("AQUA.TEST", "STEP_FAILED")
        )
        : System::StartupStepResult::succeeded();
}

System::StartupAction action(const char* name, StepContext& context) {
    return {name, &step, &context};
}

System::StartupParticipant ordinary(
    const char* name, StepContext& context, System::StartupPhase phase
) {
    return {action(name, context), phase, System::StartupRequirement::REQUIRED};
}

System::ApplicationPlan plan(
    StepContext& early, StepContext& gate,
    const System::StartupParticipant* participants, size_t count
) {
    return {action("early", early), action("gate", gate), participants, count};
}

System::StartupFailureStorage noFailures() { return {nullptr, 0U}; }

void assertFatalIdentityFailure(const System::ApplicationRuntime& runtime) {
    const System::StartupReport& report = runtime.startupReport();
    TEST_ASSERT_TRUE(report.hasFatalFailure());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::StartupFailureKind::PARTICIPANT_FAILURE),
        static_cast<int>(report.fatalFailure()->kind()));
    TEST_ASSERT_EQUAL_STRING("RUNTIME_IDENTITY_GENERATION_FAILED",
        report.fatalFailure()->errorCode().localCode());
    TEST_ASSERT_EQUAL_STRING("AQUA.CORE",
        report.fatalFailure()->errorCode().ownerNamespace());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::ERROR),
        static_cast<int>(runtime.status().operational));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::HealthState::FAULT),
        static_cast<int>(runtime.status().health));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::SafetyState::LOCKED),
        static_cast<int>(runtime.status().safety));
}

void testParticipantSuccessAndRequiredOrdering() {
    FakeGenerator generator;
    Id::RuntimeIdentityState state;
    Id::RuntimeIdentityStartup adapter(state, generator);
    StepContext early = {&state, false, false, 0U};
    StepContext boot = {&state, false, false, 0U};
    StepContext device = {&state, true, false, 0U};
    StepContext gate = {&state, true, false, 0U};
    const System::StartupParticipant participants[] = {
        ordinary("boot", boot, System::StartupPhase::BOOT),
        adapter.participant(),
        ordinary("device", device, System::StartupPhase::CORE_INIT)
    };
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::StartupRequirement::REQUIRED),
        static_cast<int>(participants[1].requirement));
    System::ApplicationRuntime runtime(plan(early, gate, participants, 3U), noFailures());
    TEST_ASSERT_FALSE(runtime.start().hasFatalFailure());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(System::OperationalState::RUNNING),
        static_cast<int>(runtime.status().operational));
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_EQUAL_UINT(1U, early.calls);
    TEST_ASSERT_EQUAL_UINT(1U, boot.calls);
    TEST_ASSERT_EQUAL_UINT(1U, device.calls);
    TEST_ASSERT_EQUAL_UINT(1U, gate.calls);
    const Id::RuntimeIdentity original = state.identity();
    runtime.start();
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_TRUE(original == state.identity());
}

void testGeneratorFailureStopsStartupWithoutIdentity() {
    FakeGenerator generator;
    generator.fail = true;
    Id::RuntimeIdentityState state;
    Id::RuntimeIdentityStartup adapter(state, generator);
    StepContext early = {&state, false, false, 0U};
    StepContext gate = {&state, false, false, 0U};
    const System::StartupParticipant participants[] = {adapter.participant()};
    System::ApplicationRuntime runtime(plan(early, gate, participants, 1U), noFailures());
    runtime.start();
    assertFatalIdentityFailure(runtime);
    TEST_ASSERT_FALSE(state.identity().isValid());
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_EQUAL_UINT(0U, gate.calls);
    runtime.start();
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
}

void testMalformedSuccessStopsStartupAsParticipantFailure() {
    FakeGenerator generator;
    generator.malformedSuccess = true;
    Id::RuntimeIdentityState state;
    Id::RuntimeIdentityStartup adapter(state, generator);
    StepContext early = {&state, false, false, 0U};
    StepContext gate = {&state, false, false, 0U};
    const System::StartupParticipant participants[] = {adapter.participant()};
    System::ApplicationRuntime runtime(plan(early, gate, participants, 1U), noFailures());
    runtime.start();
    assertFatalIdentityFailure(runtime);
    TEST_ASSERT_FALSE(state.identity().isValid());
}

void testFatalBeforeIdentityNeverGeneratesLater() {
    FakeGenerator generator;
    Id::RuntimeIdentityState state;
    Id::RuntimeIdentityStartup adapter(state, generator);
    StepContext early = {&state, false, false, 0U};
    StepContext boot = {&state, false, true, 0U};
    StepContext gate = {&state, false, false, 0U};
    const System::StartupParticipant participants[] = {
        ordinary("boot", boot, System::StartupPhase::BOOT), adapter.participant()
    };
    System::ApplicationRuntime runtime(plan(early, gate, participants, 2U), noFailures());
    runtime.start();
    TEST_ASSERT_TRUE(runtime.startupReport().hasFatalFailure());
    TEST_ASSERT_FALSE(state.identity().isValid());
    TEST_ASSERT_FALSE(state.hasAttempted());
    runtime.start();
    TEST_ASSERT_EQUAL_UINT(0U, generator.calls);
}

void testFatalAfterIdentityPreservesValue() {
    FakeGenerator generator;
    generator.values[0] = UINT64_C(0x9876543212345678);
    Id::RuntimeIdentityState state;
    Id::RuntimeIdentityStartup adapter(state, generator);
    StepContext early = {&state, false, false, 0U};
    StepContext later = {&state, true, true, 0U};
    StepContext gate = {&state, true, false, 0U};
    const System::StartupParticipant participants[] = {
        adapter.participant(), ordinary("later", later, System::StartupPhase::CORE_INIT)
    };
    System::ApplicationRuntime runtime(plan(early, gate, participants, 2U), noFailures());
    runtime.start();
    TEST_ASSERT_TRUE(runtime.startupReport().hasFatalFailure());
    TEST_ASSERT_EQUAL_UINT64(generator.values[0], state.identity().value());
    runtime.start();
    TEST_ASSERT_EQUAL_UINT(1U, generator.calls);
    TEST_ASSERT_EQUAL_UINT64(generator.values[0], state.identity().value());
}

#if defined(ARDUINO_ARCH_ESP32)
bool noEntropyWindow(void*) { return false; }
void testEsp32BackendRejectsClosedEntropyWindow() {
    Id::Esp32RuntimeIdentityGenerator generator(&noEntropyWindow, nullptr);
    Id::RuntimeIdentity result;
    TEST_ASSERT_FALSE(generator.generate(result));
    TEST_ASSERT_FALSE(result.isValid());
}
#endif

} // namespace

void runRuntimeIdentityTests() {
    RUN_TEST(testDefaultAndZeroAreUnavailable);
    RUN_TEST(testNonzeroFactoryCopyAndFullWidthEquality);
    RUN_TEST(testCanonicalFormatPreservesLeadingZeros);
    RUN_TEST(testCanonicalFormatOfMaximumValue);
    RUN_TEST(testFormatterRejectsInvalidAndSmallStorageWithoutWriting);
    RUN_TEST(testOneShotSuccessAndStableRead);
    RUN_TEST(testFailureIsOneShotAndUnavailable);
    RUN_TEST(testMalformedGeneratorSuccessIsRejected);
    RUN_TEST(testFreshOwnerInvokesGeneratorAgain);
    RUN_TEST(testParticipantSuccessAndRequiredOrdering);
    RUN_TEST(testGeneratorFailureStopsStartupWithoutIdentity);
    RUN_TEST(testMalformedSuccessStopsStartupAsParticipantFailure);
    RUN_TEST(testFatalBeforeIdentityNeverGeneratesLater);
    RUN_TEST(testFatalAfterIdentityPreservesValue);
#if defined(ARDUINO_ARCH_ESP32)
    RUN_TEST(testEsp32BackendRejectsClosedEntropyWindow);
#endif
}
