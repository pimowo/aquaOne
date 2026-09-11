#include "Button.h"

Button::Button(uint8_t pin)
    : pin_(pin)
{
}

void Button::configure(
    bool activeLow,
    bool usePullup,
    uint32_t debounceMs,
    uint32_t longPressMs
)
{
    activeLow_ = activeLow;
    usePullup_ = usePullup;

    debounceMs_ = debounceMs;
    longPressMs_ = longPressMs;
}

void Button::begin()
{
    pinMode(
        pin_,
        usePullup_ ? INPUT_PULLUP : INPUT
    );

    rawPressed_ = readPressed();
    stablePressed_ = rawPressed_;

    longPressReported_ = false;

    shortPressEvent_ = false;
    longPressEvent_ = false;

    rawChangeMs_ = millis();

    if (stablePressed_)
    {
        pressStartMs_ = millis();
    }
    else
    {
        pressStartMs_ = 0;
    }
}

void Button::update()
{
    const uint32_t now = millis();
    const bool currentRaw = readPressed();

    if (currentRaw != rawPressed_)
    {
        rawPressed_ = currentRaw;
        rawChangeMs_ = now;
    }

    if (
        stablePressed_ != rawPressed_ &&
        (now - rawChangeMs_) >= debounceMs_
    )
    {
        stablePressed_ = rawPressed_;

        if (stablePressed_)
        {
            pressStartMs_ = now;
            longPressReported_ = false;
        }
        else
        {
            if (!longPressReported_)
            {
                shortPressEvent_ = true;
            }

            pressStartMs_ = 0;
        }
    }

    if (
        stablePressed_ &&
        !longPressReported_ &&
        (now - pressStartMs_) >= longPressMs_
    )
    {
        longPressReported_ = true;
        longPressEvent_ = true;
    }
}

bool Button::isPressed() const
{
    return stablePressed_;
}

bool Button::shortPress()
{
    if (!shortPressEvent_)
    {
        return false;
    }

    shortPressEvent_ = false;
    return true;
}

bool Button::longPress()
{
    if (!longPressEvent_)
    {
        return false;
    }

    longPressEvent_ = false;
    return true;
}

bool Button::readPressed() const
{
    const bool pinHigh =
        digitalRead(pin_) == HIGH;

    return activeLow_
        ? !pinHigh
        : pinHigh;
}