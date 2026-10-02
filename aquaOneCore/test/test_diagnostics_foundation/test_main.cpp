#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Diagnostics/DiagnosticProvider.h"

void runCoreDiagnosticProviderTests();
void runDomainDiagnosticProviderTests();
void runDiagnosticRegistryTests();

using AquaCore::Diagnostics::DiagnosticProvider;
using AquaCore::Diagnostics::DiagnosticReadResult;

namespace {

struct SnapshotA {
    int value = 0;
};

struct SnapshotB {
    uint32_t count = 0U;
};

static_assert(std::is_abstract<DiagnosticProvider<SnapshotA>>::value,
    "DiagnosticProvider must be an interface");
static_assert(!std::has_virtual_destructor<DiagnosticProvider<SnapshotA>>::value,
    "Borrowed provider must not require virtual destruction");
static_assert(!std::is_convertible<DiagnosticProvider<SnapshotA>*,
                                   DiagnosticProvider<SnapshotB>*>::value,
    "Unrelated snapshot providers must remain distinct types");

class AvailableProvider final : public DiagnosticProvider<SnapshotA> {
public:
    explicit AvailableProvider(int value) : value_(value) {}

    DiagnosticReadResult read(SnapshotA& out) const override {
        out.value = value_;
        return DiagnosticReadResult::Success;
    }

private:
    int value_;
};

class UnavailableProvider final : public DiagnosticProvider<SnapshotA> {
public:
    DiagnosticReadResult read(SnapshotA& out) const override {
        out.value = 99; // Output has no validity when the read fails.
        return DiagnosticReadResult::Unavailable;
    }
};

class OtherProvider final : public DiagnosticProvider<SnapshotB> {
public:
    DiagnosticReadResult read(SnapshotB& out) const override {
        out.count = 42U;
        return DiagnosticReadResult::Success;
    }
};

class InvalidResultProvider final : public DiagnosticProvider<SnapshotA> {
public:
    DiagnosticReadResult read(SnapshotA&) const override {
        return static_cast<DiagnosticReadResult>(0xFFU);
    }
};

bool consume(const DiagnosticProvider<SnapshotA>& provider, int& value) {
    SnapshotA snapshot {};
    if (provider.read(snapshot) != DiagnosticReadResult::Success) {
        return false;
    }
    value = snapshot.value;
    return true;
}

void testBorrowedProviderFillsCallerOwnedSnapshot() {
    AvailableProvider concrete(17);
    const DiagnosticProvider<SnapshotA>& borrowed = concrete;
    SnapshotA output {};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(borrowed.read(output)));
    TEST_ASSERT_EQUAL_INT(17, output.value);
}

void testUnavailableDoesNotPublishOutput() {
    UnavailableProvider concrete;
    const DiagnosticProvider<SnapshotA>& borrowed = concrete;
    int published = 7;

    TEST_ASSERT_FALSE(consume(borrowed, published));
    TEST_ASSERT_EQUAL_INT(7, published);
}

void testTypedProvidersAreIndependent() {
    AvailableProvider first(3);
    OtherProvider second;
    const DiagnosticProvider<SnapshotA>& firstBorrowed = first;
    const DiagnosticProvider<SnapshotB>& secondBorrowed = second;
    SnapshotA firstOutput {};
    SnapshotB secondOutput {};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(firstBorrowed.read(firstOutput)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(secondBorrowed.read(secondOutput)));
    TEST_ASSERT_EQUAL_INT(3, firstOutput.value);
    TEST_ASSERT_EQUAL_UINT32(42U, secondOutput.count);
}

void testInvalidResultAlsoLeavesNoValidSnapshot() {
    InvalidResultProvider concrete;
    int published = 7;

    TEST_ASSERT_FALSE(consume(concrete, published));
    TEST_ASSERT_EQUAL_INT(7, published);
}

} // namespace

void setUp() {}
void tearDown() {}

void runTests() {
    RUN_TEST(testBorrowedProviderFillsCallerOwnedSnapshot);
    RUN_TEST(testUnavailableDoesNotPublishOutput);
    RUN_TEST(testTypedProvidersAreIndependent);
    RUN_TEST(testInvalidResultAlsoLeavesNoValidSnapshot);
    runCoreDiagnosticProviderTests();
    runDomainDiagnosticProviderTests();
    runDiagnosticRegistryTests();
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    runTests();
    UNITY_END();
}

void loop() {}
#else
int main() {
    UNITY_BEGIN();
    runTests();
    return UNITY_END();
}
#endif
