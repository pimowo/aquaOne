#include "Pump.h"

Pump::Pump(uint8_t pin)
    : pin_(pin)
{
}

void Pump::begin()
{
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);

    isOn_ = false;
}

void Pump::on()
{
    digitalWrite(pin_, HIGH);
    isOn_ = true;
}

void Pump::off()
{
    digitalWrite(pin_, LOW);
    isOn_ = false;
}

bool Pump::isOn() const
{
    return isOn_;
}
