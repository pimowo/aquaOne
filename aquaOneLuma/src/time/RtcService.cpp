#include "RtcService.h"

#include <Arduino.h>
#ifdef LUMASENSE_RTC_WIRE_HEADER
#include LUMASENSE_RTC_WIRE_HEADER
#else
#include <Wire.h>
#endif

#include "../../include/BuildConfig.h"

namespace LumaSense {
namespace {

class LumaRtcBus final : public AquaCore::Time::RtcBus {
public:
    bool begin(int sdaPin, int sclPin) override {
        return Wire.begin(sdaPin, sclPin);
    }

    bool readRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        uint8_t* buffer,
        size_t length
    ) override {
        Wire.beginTransmission(deviceAddress);

        if (Wire.write(startRegister) != 1U) {
            return false;
        }

        if (Wire.endTransmission(false) != 0U) {
            return false;
        }

        const size_t received =
            Wire.requestFrom(deviceAddress, length);

        if (received != length) {
            return false;
        }

        for (size_t index = 0U; index < length; ++index) {
            const int value = Wire.read();

            if (value < 0) {
                return false;
            }

            buffer[index] = static_cast<uint8_t>(value);
        }

        return true;
    }

    bool writeRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        const uint8_t* buffer,
        size_t length
    ) override {
        Wire.beginTransmission(deviceAddress);

        if (Wire.write(startRegister) != 1U) {
            return false;
        }

        for (size_t index = 0U; index < length; ++index) {
            if (Wire.write(buffer[index]) != 1U) {
                return false;
            }
        }

        return Wire.endTransmission() == 0U;
    }
};

AquaCore::Time::RtcBus& lumaRtcBus() {
    static LumaRtcBus bus;
    return bus;
}

AquaCore::Time::RtcConfig lumaRtcConfig() {
    AquaCore::Time::RtcConfig config {};
    config.sdaPin = LUMASENSE_RTC_SDA_PIN;
    config.sclPin = LUMASENSE_RTC_SCL_PIN;
    config.i2cAddress = 0x68U;
    return config;
}

} // namespace

RtcService::RtcService()
    : AquaCore::Time::RtcService(
          lumaRtcBus(),
          lumaRtcConfig()
      ) {
}

} // namespace LumaSense