#pragma once

#include <stdint.h>

#include "AquaCore/Time/RtcBus.h"
#include "AquaCore/Time/TimeTypes.h"

namespace AquaCore {
namespace Time {

struct RtcConfig {
    int sdaPin = -1;
    int sclPin = -1;
    uint8_t i2cAddress = 0x68U;
};

class RtcService {
public:
    RtcService(
        RtcBus& bus,
        const RtcConfig& config
    );

    bool begin();
    LocalTime read();

    bool isValid() const;
    bool isInitialized() const;

    static bool decodeDateTimeRegisters(
        const uint8_t data[7],
        LocalTime& result
    );

    static bool isValidUtc(
        uint16_t year,
        uint8_t month,
        uint8_t day,
        uint8_t hour,
        uint8_t minute,
        uint8_t second
    );

    bool setUtc(
        uint16_t year,
        uint8_t month,
        uint8_t day,
        uint8_t hour,
        uint8_t minute,
        uint8_t second
    );

private:
    RtcBus& bus_;
    RtcConfig config_;
    bool initialized_ = false;
    bool valid_ = false;
};

} // namespace Time
} // namespace AquaCore