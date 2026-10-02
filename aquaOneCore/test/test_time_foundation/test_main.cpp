#if defined(ARDUINO)
#include <Arduino.h>
#include "AquaCore/Time/Esp32MonotonicClock.h"
#endif

#include <stdint.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Time/MonotonicClock.h"

using AquaCore::Time::MonotonicClock;

static_assert(std::is_abstract<MonotonicClock>::value,
    "MonotonicClock is a capability interface");
static_assert(!std::has_virtual_destructor<MonotonicClock>::value,
    "Borrowed clock must not require virtual destruction");

namespace {

class FakeClock final : public MonotonicClock {
public:
    explicit FakeClock(uint64_t initial) : nowMs_(initial) {}

    uint64_t nowMilliseconds() const override { return nowMs_; }
    void advance(uint64_t elapsedMs) { nowMs_ += elapsedMs; }

private:
    uint64_t nowMs_;
};

void testInitialValueAndAdvance() {
    FakeClock concrete(1000ULL);
    const MonotonicClock& clock = concrete;
    TEST_ASSERT_EQUAL_UINT64(1000ULL, clock.nowMilliseconds());
    concrete.advance(250ULL);
    TEST_ASSERT_EQUAL_UINT64(1250ULL, clock.nowMilliseconds());
}

void testEqualReadsAndNondecreasingAdvance() {
    FakeClock concrete(1000ULL);
    const MonotonicClock& clock = concrete;
    const uint64_t first = clock.nowMilliseconds();
    const uint64_t second = clock.nowMilliseconds();
    TEST_ASSERT_TRUE(second >= first);
    TEST_ASSERT_EQUAL_UINT64(first, second);
    concrete.advance(1ULL);
    TEST_ASSERT_TRUE(clock.nowMilliseconds() >= second);
    TEST_ASSERT_EQUAL_UINT64(1001ULL, clock.nowMilliseconds());
}

void testFull64BitValue() {
    FakeClock concrete(static_cast<uint64_t>(UINT32_MAX) + 1000ULL);
    const MonotonicClock& clock = concrete;
    TEST_ASSERT_EQUAL_UINT64(
        static_cast<uint64_t>(UINT32_MAX) + 1000ULL,
        clock.nowMilliseconds()
    );
}

void testDurationArithmeticAcross32BitBoundary() {
    FakeClock concrete(static_cast<uint64_t>(UINT32_MAX) - 20ULL);
    const MonotonicClock& clock = concrete;
    const uint64_t start = clock.nowMilliseconds();
    concrete.advance(250ULL);
    const uint64_t elapsed = clock.nowMilliseconds() - start;
    TEST_ASSERT_EQUAL_UINT64(250ULL, elapsed);
}

#if defined(ARDUINO)
void testEsp32AdapterThroughBorrowedReference() {
    AquaCore::Time::Esp32MonotonicClock concrete;
    const MonotonicClock& clock = concrete;
    const uint64_t first = clock.nowMilliseconds();
    const uint64_t second = clock.nowMilliseconds();
    TEST_ASSERT_TRUE(second >= first);
}
#endif

} // namespace

void setUp() {}
void tearDown() {}

void runTests() {
    RUN_TEST(testInitialValueAndAdvance);
    RUN_TEST(testEqualReadsAndNondecreasingAdvance);
    RUN_TEST(testFull64BitValue);
    RUN_TEST(testDurationArithmeticAcross32BitBoundary);
#if defined(ARDUINO)
    RUN_TEST(testEsp32AdapterThroughBorrowedReference);
#endif
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
