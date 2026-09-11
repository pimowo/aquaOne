#pragma once

#include <Arduino.h>

class Buzzer
{
public:
    explicit Buzzer(uint8_t pin);

    void configure(
        bool activeHigh = true
    );

    void begin();

    void on();
    void off();

    bool isOn() const;

private:
    void writeState(bool enabled);

    uint8_t pin_;

    bool activeHigh_ = true;
    bool isOn_ = false;
};