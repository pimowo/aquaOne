#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Time/RtcBus.h"
#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/RtcWallClock.h"

using AquaCore::Time::RtcBus;
using AquaCore::Time::RtcConfig;
using AquaCore::Time::RtcService;
using AquaCore::Time::RtcWallClock;
using AquaCore::Time::UtcTimestamp;
using AquaCore::Time::WallClock;
using AquaCore::Time::WallClockReadResult;

static_assert(!std::is_constructible<RtcWallClock, RtcService&&>::value,
    "RtcWallClock must reject temporary RTC sources");

namespace {

class FakeRtcBus final : public RtcBus {
public:
    bool begin(int, int) override { return beginResult; }

    bool readRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        uint8_t* output,
        size_t length
    ) override {
        if (
            failReads ||
            deviceAddress != expectedAddress ||
            output == nullptr ||
            static_cast<size_t>(startRegister) + length > sizeof(registers)
        ) {
            return false;
        }
        memcpy(output, &registers[startRegister], length);
        return true;
    }

    bool writeRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        const uint8_t* input,
        size_t length
    ) override {
        if (
            deviceAddress != expectedAddress ||
            input == nullptr ||
            static_cast<size_t>(startRegister) + length > sizeof(registers)
        ) {
            return false;
        }
        memcpy(&registers[startRegister], input, length);
        return true;
    }

    bool beginResult = true;
    bool failReads = false;
    uint8_t expectedAddress = 0x68U;
    uint8_t registers[256] {};
};

uint8_t bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

void setRtcUtc(
    FakeRtcBus& bus,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    bus.registers[0x00] = bcd(second);
    bus.registers[0x01] = bcd(minute);
    bus.registers[0x02] = bcd(hour);
    bus.registers[0x03] = bcd(1U);
    bus.registers[0x04] = bcd(day);
    bus.registers[0x05] = bcd(month);
    if (year >= 2100U) {
        bus.registers[0x05] |= 0x80U;
    }
    bus.registers[0x06] = bcd(static_cast<uint8_t>(year % 100U));
    bus.registers[0x0F] = 0U;
}

void testRtcWallClockValidRtcReturnsUnixTimestamp() {
    FakeRtcBus bus;
    setRtcUtc(bus, 2000U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, RtcConfig {});
    TEST_ASSERT_TRUE(rtc.begin());
    RtcWallClock concrete(rtc);
    const WallClock& clock = concrete;
    UtcTimestamp utc {};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(946684800ULL, utc.secondsSinceUnixEpoch);
}

void testRtcWallClockUnavailableBeforeRtcBeginAndAfterReadFailure() {
    FakeRtcBus bus;
    setRtcUtc(bus, 2026U, 1U, 2U, 3U, 4U, 5U);
    RtcService rtc(bus, RtcConfig {});
    RtcWallClock clock(rtc);
    UtcTimestamp ignored {};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Unavailable),
        static_cast<int>(clock.readUtc(ignored))
    );
    TEST_ASSERT_TRUE(rtc.begin());
    bus.failReads = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Unavailable),
        static_cast<int>(clock.readUtc(ignored))
    );
}

void testRtcWallClockRejectsOsfAndInvalidCalendar() {
    FakeRtcBus bus;
    setRtcUtc(bus, 2026U, 1U, 2U, 3U, 4U, 5U);
    RtcService rtc(bus, RtcConfig {});
    TEST_ASSERT_TRUE(rtc.begin());
    RtcWallClock clock(rtc);
    UtcTimestamp ignored {};

    bus.registers[0x0F] = 0x80U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Unavailable),
        static_cast<int>(clock.readUtc(ignored))
    );

    bus.registers[0x0F] = 0U;
    bus.registers[0x05] = bcd(13U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Unavailable),
        static_cast<int>(clock.readUtc(ignored))
    );
}

void testRtcWallClockReadsLiveSourceWithoutCache() {
    FakeRtcBus bus;
    setRtcUtc(bus, 2026U, 1U, 1U, 0U, 0U, 0U);
    RtcService rtc(bus, RtcConfig {});
    TEST_ASSERT_TRUE(rtc.begin());
    RtcWallClock clock(rtc);
    UtcTimestamp first {};
    UtcTimestamp second {};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(first))
    );
    setRtcUtc(bus, 2026U, 1U, 1U, 0U, 0U, 10U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(second))
    );
    TEST_ASSERT_EQUAL_UINT64(10ULL,
        second.secondsSinceUnixEpoch - first.secondsSinceUnixEpoch);
}

void testRtcWallClockConvertsLeapDayCorrectly() {
    FakeRtcBus bus;
    setRtcUtc(bus, 2000U, 2U, 29U, 0U, 0U, 0U);
    RtcService rtc(bus, RtcConfig {});
    TEST_ASSERT_TRUE(rtc.begin());
    RtcWallClock clock(rtc);
    UtcTimestamp utc {};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WallClockReadResult::Success),
        static_cast<int>(clock.readUtc(utc))
    );
    TEST_ASSERT_EQUAL_UINT64(951782400ULL, utc.secondsSinceUnixEpoch);
}

} // namespace

void runRtcWallClockTests() {
    RUN_TEST(testRtcWallClockValidRtcReturnsUnixTimestamp);
    RUN_TEST(testRtcWallClockUnavailableBeforeRtcBeginAndAfterReadFailure);
    RUN_TEST(testRtcWallClockRejectsOsfAndInvalidCalendar);
    RUN_TEST(testRtcWallClockReadsLiveSourceWithoutCache);
    RUN_TEST(testRtcWallClockConvertsLeapDayCorrectly);
}
