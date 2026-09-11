#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Time {

class RtcBus {
public:
    virtual ~RtcBus() = default;

    virtual bool begin(int sdaPin, int sclPin) = 0;

    virtual bool readRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        uint8_t* buffer,
        size_t length
    ) = 0;

    virtual bool writeRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        const uint8_t* buffer,
        size_t length
    ) = 0;
};

} // namespace Time
} // namespace AquaCore