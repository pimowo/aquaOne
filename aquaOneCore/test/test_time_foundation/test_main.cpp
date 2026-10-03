#if defined(ARDUINO)
#include <Arduino.h>
#include "AquaCore/Time/Esp32MonotonicClock.h"
#endif

#include <stdint.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Time/MonotonicClock.h"
#include "AquaCore/Time/WallClock.h"

using AquaCore::Time::MonotonicClock;
using AquaCore::Time::UtcTimestamp;
using AquaCore::Time::WallClock;
using AquaCore::Time::WallClockReadResult;

void runRtcWallClockTests();
void runNtpIntegrationTests();

static_assert(std::is_abstract<MonotonicClock>::value,
    "MonotonicClock is a capability interface");
static_assert(!std::has_virtual_destructor<MonotonicClock>::value,
    "Borrowed clock must not require virtual destruction");
static_assert(std::is_abstract<WallClock>::value,
    "WallClock is a capability interface");
static_assert(!std::has_virtual_destructor<WallClock>::value,
    "Borrowed wall clock must not require virtual destruction");

namespace {

class FakeClock final : public MonotonicClock {
public:
    explicit FakeClock(uint64_t initial) : nowMs_(initial) {}

    uint64_t nowMilliseconds() const override { return nowMs_; }
    void advance(uint64_t elapsedMs) { nowMs_ += elapsedMs; }

private:
    uint64_t nowMs_;
};

class FakeWallClock final : public WallClock {
public:
    FakeWallClock(bool available, uint64_t seconds)
        : available_(available), seconds_(seconds) {}

    WallClockReadResult readUtc(UtcTimestamp& out) const override {
        if (!available_) {
            return WallClockReadResult::Unavailable;
        }
        out.secondsSinceUnixEpoch = seconds_;
        return WallClockReadResult::Success;
    }

    void setAvailable(bool available) { available_ = available; }
    void setSeconds(uint64_t seconds) { seconds_ = seconds; }

private:
    bool available_;
    uint64_t seconds_;
};

bool readForPublication(const WallClock& clock, UtcTimestamp& published) {
    UtcTimestamp candidate {};
    if (clock.readUtc(candidate) != WallClockReadResult::Success) {
        return false;
    }
    published = candidate;
    return true;
}

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

void testWallClockSuccessThroughBorrowedReference() {
    FakeWallClock concrete(true, 1000000ULL);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(1000000ULL, utc.secondsSinceUnixEpoch);
}

void testWallClockUnavailableDoesNotPublishOutput() {
    FakeWallClock concrete(false, 1234ULL);
    const WallClock& clock = concrete;
    UtcTimestamp ignored {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Unavailable),
        static_cast<int>(clock.readUtc(ignored))
    );

    UtcTimestamp published {};
    published.secondsSinceUnixEpoch = 77ULL;
    TEST_ASSERT_FALSE(readForPublication(clock, published));
    TEST_ASSERT_EQUAL_UINT64(77ULL, published.secondsSinceUnixEpoch);

    concrete.setAvailable(true);
    TEST_ASSERT_TRUE(readForPublication(clock, published));
    TEST_ASSERT_EQUAL_UINT64(1234ULL, published.secondsSinceUnixEpoch);
}

void testWallClockZeroIsValid() {
    FakeWallClock concrete(true, 0ULL);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};
    utc.secondsSinceUnixEpoch = 91ULL;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(0ULL, utc.secondsSinceUnixEpoch);
}

void testWallClockPreservesFull64BitValue() {
    const uint64_t expected = static_cast<uint64_t>(UINT32_MAX) + 1000ULL;
    FakeWallClock concrete(true, expected);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(expected, utc.secondsSinceUnixEpoch);
}

void testWallClockMayMoveForward() {
    FakeWallClock concrete(true, 1000ULL);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(1000ULL, utc.secondsSinceUnixEpoch);
    concrete.setSeconds(2000ULL);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(2000ULL, utc.secondsSinceUnixEpoch);
}

void testWallClockMayMoveBackward() {
    FakeWallClock concrete(true, 2000ULL);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(2000ULL, utc.secondsSinceUnixEpoch);
    concrete.setSeconds(1500ULL);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(1500ULL, utc.secondsSinceUnixEpoch);
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
    RUN_TEST(testWallClockSuccessThroughBorrowedReference);
    RUN_TEST(testWallClockUnavailableDoesNotPublishOutput);
    RUN_TEST(testWallClockZeroIsValid);
    RUN_TEST(testWallClockPreservesFull64BitValue);
    RUN_TEST(testWallClockMayMoveForward);
    RUN_TEST(testWallClockMayMoveBackward);
    runRtcWallClockTests();
    runNtpIntegrationTests();
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
