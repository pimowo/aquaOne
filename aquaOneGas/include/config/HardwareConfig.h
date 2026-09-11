#pragma once
#include <stdint.h>

namespace gassense {

struct HardwareConfig {
    static constexpr uint8_t HX711_DATA = 1;
    static constexpr uint8_t HX711_CLK  = 2;

    static constexpr uint8_t I2C_SDA = 9;
    static constexpr uint8_t I2C_SCL = 10;

    static constexpr uint8_t BUTTON = 11;
    static constexpr uint8_t BUZZER = 12;

    static constexpr uint8_t DS18B20 = 13;
};

} // namespace gassense
