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
    /*
     * Tak jak przy pompie:
     * najpierw wymuszamy stan bezpieczny,
     * potem ustawiamy GPIO jako OUTPUT.
     */
    digitalWrite(
        pin_,
        activeHigh_ ? LOW : HIGH
    );

    pinMode(pin_, OUTPUT);

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