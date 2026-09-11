#pragma once

#include <Arduino.h>

namespace BoardPins
{
    constexpr uint8_t PUMP = 1;

    constexpr uint8_t ULTRASONIC_TRIG = 2;
    constexpr uint8_t ULTRASONIC_ECHO = 3;

    constexpr uint8_t FLOAT_SENSOR = 4;

    constexpr uint8_t I2C_SDA = 9;
    constexpr uint8_t I2C_SCL = 10;

    constexpr uint8_t BUTTON = 11;
    constexpr uint8_t BUZZER = 12;

    constexpr uint8_t TEMP_RO = 13;
}