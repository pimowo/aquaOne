#pragma once

#include <stddef.h>
#include <stdint.h>

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define LOW 0x00
#define HIGH 0x01

namespace ArduinoTest
{
enum class GpioOperationKind : uint8_t
{
    PinMode,
    DigitalWrite
};

struct GpioOperation
{
    GpioOperationKind kind;
    uint8_t pin;
    uint8_t value;
};

extern GpioOperation gpioOperations[8U];
extern size_t gpioOperationCount;

void resetGpioOperations();
void recordPinMode(uint8_t pin, uint8_t mode);
void recordDigitalWrite(uint8_t pin, uint8_t value);
}

inline void pinMode(uint8_t pin, uint8_t mode)
{
    ArduinoTest::recordPinMode(pin, mode);
}

inline void digitalWrite(uint8_t pin, uint8_t value)
{
    ArduinoTest::recordDigitalWrite(pin, value);
}

inline int digitalRead(uint8_t)
{
    return LOW;
}

inline void delayMicroseconds(uint32_t)
{
}

inline uint32_t pulseIn(uint8_t, uint8_t, uint32_t)
{
    return 0U;
}
