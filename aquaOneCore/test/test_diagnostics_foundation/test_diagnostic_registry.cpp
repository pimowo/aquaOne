#include <unity.h>

#include <type_traits>
#include <utility>

#include <AquaCore/Diagnostics/DiagnosticRegistry.h>

using AquaCore::Diagnostics::DiagnosticRegistry;

namespace {

struct FakeEntry {
    int id;
};

static_assert(std::is_same<
    decltype(std::declval<const DiagnosticRegistry<FakeEntry>&>().entryAt(0U)),
    const FakeEntry*>::value, "Registry enumeration must be read-only");

void testRegistryEnumeratesCallerOwnedEntriesInOrder() {
    const FakeEntry entries[] = {{7}, {3}, {11}};
    const DiagnosticRegistry<FakeEntry> registry(entries, 3U);

    TEST_ASSERT_TRUE(registry.isValid());
    TEST_ASSERT_EQUAL_UINT32(3U, registry.size());
    TEST_ASSERT_EQUAL_PTR(&entries[0], registry.entryAt(0U));
    TEST_ASSERT_EQUAL_PTR(&entries[1], registry.entryAt(1U));
    TEST_ASSERT_EQUAL_PTR(&entries[2], registry.entryAt(2U));
    TEST_ASSERT_EQUAL_INT(7, registry.entryAt(0U)->id);
    TEST_ASSERT_EQUAL_INT(3, registry.entryAt(1U)->id);
    TEST_ASSERT_EQUAL_INT(11, registry.entryAt(2U)->id);
}

void testEmptyRegistryIsValid() {
    const DiagnosticRegistry<FakeEntry> registry(nullptr, 0U);

    TEST_ASSERT_TRUE(registry.isValid());
    TEST_ASSERT_EQUAL_UINT32(0U, registry.size());
    TEST_ASSERT_NULL(registry.entryAt(0U));
}

void testNonNullZeroCountIsValid() {
    const FakeEntry entry {7};
    const DiagnosticRegistry<FakeEntry> registry(&entry, 0U);

    TEST_ASSERT_TRUE(registry.isValid());
    TEST_ASSERT_EQUAL_UINT32(0U, registry.size());
    TEST_ASSERT_NULL(registry.entryAt(0U));
}

void testInvalidRegistryExposesNoEntries() {
    const DiagnosticRegistry<FakeEntry> registry(nullptr, 2U);

    TEST_ASSERT_FALSE(registry.isValid());
    TEST_ASSERT_EQUAL_UINT32(0U, registry.size());
    TEST_ASSERT_NULL(registry.entryAt(0U));
    TEST_ASSERT_NULL(registry.entryAt(1U));
}

void testOutOfRangeReturnsNull() {
    const FakeEntry entries[] = {{7}, {3}};
    const DiagnosticRegistry<FakeEntry> registry(entries, 2U);

    TEST_ASSERT_NULL(registry.entryAt(registry.size()));
    TEST_ASSERT_NULL(registry.entryAt(registry.size() + 5U));
}

} // namespace

void runDiagnosticRegistryTests() {
    RUN_TEST(testRegistryEnumeratesCallerOwnedEntriesInOrder);
    RUN_TEST(testEmptyRegistryIsValid);
    RUN_TEST(testNonNullZeroCountIsValid);
    RUN_TEST(testInvalidRegistryExposesNoEntries);
    RUN_TEST(testOutOfRangeReturnsNull);
}
