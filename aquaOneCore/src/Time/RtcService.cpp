#include "AquaCore/Time/RtcService.h"

namespace AquaCore {
namespace Time {

namespace {

constexpr uint8_t RTC_STATUS_REGISTER = 0x0F;
constexpr uint8_t RTC_OSF_MASK = 0x80U;

bool decodeBcd(
    uint8_t value,
    uint8_t maximum,
    uint8_t& decoded
) {
    const uint8_t lowDigit = value & 0x0FU;
    const uint8_t highDigit = value >> 4;

    if (lowDigit > 9U || highDigit > 9U) {
        return false;
    }

    decoded = static_cast<uint8_t>(
        highDigit * 10U + lowDigit
    );

    return decoded <= maximum;
}

uint8_t decToBcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4) | (value % 10U)
    );
}

bool isRtcLeapYear(uint16_t year) {
    return
        (year % 4U == 0U && year % 100U != 0U) ||
        year % 400U == 0U;
}

uint8_t rtcDaysInMonth(
    uint16_t year,
    uint8_t month
) {
    static constexpr uint8_t DAYS[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1U || month > 12U) {
        return 0;
    }

    if (month == 2U && isRtcLeapYear(year)) {
        return 29;
    }

    return DAYS[month - 1U];
}

bool sameDateTime(
    const LocalTime& value,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    return
        value.year == year &&
        value.month == month &&
        value.day == day &&
        value.hour == hour &&
        value.minute == minute &&
        value.second == second;
}

void addOneSecond(LocalTime& value) {
    if (++value.second <= 59U) {
        return;
    }

    value.second = 0;

    if (++value.minute <= 59U) {
        return;
    }

    value.minute = 0;

    if (++value.hour <= 23U) {
        return;
    }

    value.hour = 0;

    if (++value.day <= rtcDaysInMonth(value.year, value.month)) {
        return;
    }

    value.day = 1;

    if (++value.month <= 12U) {
        return;
    }

    value.month = 1;
    ++value.year;
}

bool matchesWrittenTime(
    const LocalTime& value,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    if (
        sameDateTime(
            value,
            year,
            month,
            day,
            hour,
            minute,
            second
        )
    ) {
        return true;
    }

    LocalTime oneSecondLater {};
    oneSecondLater.year = year;
    oneSecondLater.month = month;
    oneSecondLater.day = day;
    oneSecondLater.hour = hour;
    oneSecondLater.minute = minute;
    oneSecondLater.second = second;

    addOneSecond(oneSecondLater);

    return sameDateTime(
        value,
        oneSecondLater.year,
        oneSecondLater.month,
        oneSecondLater.day,
        oneSecondLater.hour,
        oneSecondLater.minute,
        oneSecondLater.second
    );
}

bool readRegisters(
    RtcBus& bus,
    uint8_t deviceAddress,
    uint8_t startRegister,
    uint8_t* buffer,
    size_t length
) {
    return bus.readRegisters(
        deviceAddress,
        startRegister,
        buffer,
        length
    );
}

bool writeRegisters(
    RtcBus& bus,
    uint8_t deviceAddress,
    uint8_t startRegister,
    const uint8_t* buffer,
    size_t length
) {
    return bus.writeRegisters(
        deviceAddress,
        startRegister,
        buffer,
        length
    );
}
} // namespace

RtcService::RtcService(
    RtcBus& bus,
    const RtcConfig& config
) : bus_(bus),
    config_(config) {
}
bool RtcService::begin() {
    initialized_ = bus_.begin(
        config_.sdaPin,
        config_.sclPin
    );

    valid_ = false;

    if (!initialized_) {
        return false;
    }

    uint8_t statusRegister = 0;

    if (!readRegisters(bus_, config_.i2cAddress, 
        RTC_STATUS_REGISTER,
        &statusRegister,
        1U
    )) {
        return false;
    }

    valid_ =
        (statusRegister & RTC_OSF_MASK) == 0U;

    return true;
}

LocalTime RtcService::read() {
    LocalTime result {};

    if (!initialized_) {
        valid_ = false;
        return result;
    }

    uint8_t statusRegister = 0;

    if (!readRegisters(bus_, config_.i2cAddress, 
        RTC_STATUS_REGISTER,
        &statusRegister,
        1U
    )) {
        valid_ = false;
        return result;
    }

    if ((statusRegister & RTC_OSF_MASK) != 0U) {
        valid_ = false;
        return result;
    }

    uint8_t data[7] {};

    if (!readRegisters(bus_, config_.i2cAddress, 
        0x00,
        data,
        sizeof(data)
    )) {
        valid_ = false;
        return result;
    }

    if (!decodeDateTimeRegisters(data, result)) {
        valid_ = false;
        return {};
    }

    valid_ = true;
    return result;
}

bool RtcService::isValid() const {
    return initialized_ && valid_;
}
bool RtcService::isInitialized() const {
    return initialized_;
}

bool RtcService::decodeDateTimeRegisters(
    const uint8_t data[7],
    LocalTime& result
) {
    result = {};

    if (data == nullptr) {
        return false;
    }

    uint8_t second = 0;
    uint8_t minute = 0;
    uint8_t hour = 0;
    uint8_t day = 0;
    uint8_t month = 0;
    uint8_t yearPart = 0;

    if (
        (data[0] & 0x80U) != 0U ||
        !decodeBcd(data[0], 59U, second)
    ) {
        return false;
    }

    if (
        (data[1] & 0x80U) != 0U ||
        !decodeBcd(data[1], 59U, minute)
    ) {
        return false;
    }

    const uint8_t hourRegister = data[2];

    if ((hourRegister & 0x80U) != 0U) {
        return false;
    }

    if ((hourRegister & 0x40U) != 0U) {
        uint8_t hour12 = 0;

        if (
            !decodeBcd(
                hourRegister & 0x1FU,
                12U,
                hour12
            ) ||
            hour12 < 1U
        ) {
            return false;
        }

        hour = static_cast<uint8_t>(
            hour12 % 12U
        );

        if ((hourRegister & 0x20U) != 0U) {
            hour = static_cast<uint8_t>(hour + 12U);
        }
    } else {
        if (!decodeBcd(
            hourRegister & 0x3FU,
            23U,
            hour
        )) {
            return false;
        }
    }

    if (
        (data[4] & 0xC0U) != 0U ||
        !decodeBcd(data[4], 31U, day)
    ) {
        return false;
    }

    if (
        (data[5] & 0x60U) != 0U ||
        !decodeBcd(data[5] & 0x1FU, 12U, month)
    ) {
        return false;
    }

    if (!decodeBcd(data[6], 99U, yearPart)) {
        return false;
    }

    const uint16_t year =
        static_cast<uint16_t>(
            2000U +
            ((data[5] & 0x80U) != 0U ? 100U : 0U) +
            yearPart
        );

    if (!isValidUtc(
        year,
        month,
        day,
        hour,
        minute,
        second
    )) {
        return false;
    }

    result.valid = true;
    result.year = year;
    result.month = month;
    result.day = day;
    result.hour = hour;
    result.minute = minute;
    result.second = second;
    result.minuteOfDay =
        static_cast<uint16_t>(hour * 60U + minute);

    return true;
}

bool RtcService::setUtc(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    if (!initialized_) {
        return false;
    }

    if (!isValidUtc(
        year,
        month,
        day,
        hour,
        minute,
        second
    )) {
        return false;
    }

    uint8_t data[7] {};

    data[0] = decToBcd(second);
    data[1] = decToBcd(minute);
    data[2] = decToBcd(hour);

    // Day-of-week is not used by the application.
    data[3] = decToBcd(1U);

    data[4] = decToBcd(day);
    data[5] = decToBcd(month);

    if (year >= 2100U) {
        data[5] |= 0x80U;
    }

    data[6] = decToBcd(
        static_cast<uint8_t>(year % 100U)
    );

    if (!writeRegisters(bus_, config_.i2cAddress, 
        0x00,
        data,
        sizeof(data)
    )) {
        valid_ = false;
        return false;
    }

    uint8_t statusRegister = 0;

    if (!readRegisters(bus_, config_.i2cAddress, 
        RTC_STATUS_REGISTER,
        &statusRegister,
        1U
    )) {
        valid_ = false;
        return false;
    }

    statusRegister &=
        static_cast<uint8_t>(~RTC_OSF_MASK);

    if (!writeRegisters(bus_, config_.i2cAddress, 
        RTC_STATUS_REGISTER,
        &statusRegister,
        1U
    )) {
        valid_ = false;
        return false;
    }

    uint8_t verifiedStatus = 0;

    if (
        !readRegisters(bus_, config_.i2cAddress, 
            RTC_STATUS_REGISTER,
            &verifiedStatus,
            1U
        ) ||
        (verifiedStatus & RTC_OSF_MASK) != 0U
    ) {
        valid_ = false;
        return false;
    }

    uint8_t verifiedData[7] {};
    LocalTime verifiedTime {};

    if (
        !readRegisters(bus_, config_.i2cAddress, 
            0x00,
            verifiedData,
            sizeof(verifiedData)
        ) ||
        !decodeDateTimeRegisters(
            verifiedData,
            verifiedTime
        ) ||
        !matchesWrittenTime(
            verifiedTime,
            year,
            month,
            day,
            hour,
            minute,
            second
        )
    ) {
        valid_ = false;
        return false;
    }

    valid_ = true;
    return true;
}

bool RtcService::isValidUtc(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
) {
    if (
        year < 2000U ||
        year > 2199U ||
        month < 1U ||
        month > 12U ||
        hour > 23U ||
        minute > 59U ||
        second > 59U
    ) {
        return false;
    }

    const uint8_t maximumDay =
        rtcDaysInMonth(year, month);

    return day >= 1U && day <= maximumDay;
}
} // namespace Time
} // namespace AquaCore
