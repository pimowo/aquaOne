#include "Buzzer.h"

Buzzer::Buzzer(uint8_t pin)
    : pin_(pin)
{
}

void Buzzer::configure(
    bool activeHigh
)
{
    activeHigh_ = activeHigh;
}

void Buzzer::begin()
{
    pinMode(pin_, OUTPUT);

    digitalWrite(
        pin_,
        activeHigh_ ? LOW : HIGH
    );

    isOn_ = false;
}

void Buzzer::on()
{
    writeState(true);
}

void Buzzer::off()
{
    writeState(false);
}

bool Buzzer::isOn() const
{
    return isOn_;
}

void Buzzer::writeState(
    bool enabled
)
{
    digitalWrite(
        pin_,
        enabled
            ? (activeHigh_ ? HIGH : LOW)
            : (activeHigh_ ? LOW : HIGH)
    );

    isOn_ = enabled;
}
