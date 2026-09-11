#pragma once

#include <Arduino.h>

class Pump
{
public:
    explicit Pump(uint8_t pin);

    void begin();

    void on();
    void off();

    bool isOn() const;

private:
    uint8_t pin_;
    bool isOn_ = false;
};