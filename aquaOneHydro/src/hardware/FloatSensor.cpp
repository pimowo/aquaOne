#include "FloatSensor.h"

FloatSensor::FloatSensor(uint8_t pin)
    : pin_(pin)
{
}

void FloatSensor::configure(
    bool activeLow,
    bool usePullup,
    uint32_t debounceMs
)
{
    activeLow_ = activeLow;
    usePullup_ = usePullup;
    debounceMs_ = debounceMs;
}

void FloatSensor::begin()
{
    pinMode(
        pin_,
        usePullup_ ? INPUT_PULLUP : INPUT
    );

    rawState_ = readActive();
    stableState_ = rawState_;
    lastChangeMs_ = millis();
}

void FloatSensor::update()
{
    const bool currentState = readActive();
    const uint32_t now = millis();

    if (currentState != rawState_)
    {
        rawState_ = currentState;
        lastChangeMs_ = now;
    }

    if (stableState_ != rawState_)
    {
        if ((now - lastChangeMs_) >= debounceMs_)
        {
            stableState_ = rawState_;
        }
    }
}

bool FloatSensor::isActive() const
{
    return stableState_;
}

bool FloatSensor::rawActive() const
{
    return rawState_;
}

bool FloatSensor::readActive() const
{
    const bool pinHigh =
        digitalRead(pin_) == HIGH;

    return activeLow_
        ? !pinHigh
        : pinHigh;
}