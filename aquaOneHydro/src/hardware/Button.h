#pragma once

#include <Arduino.h>

class Button
{
public:
    explicit Button(uint8_t pin);

    void configure(
        bool activeLow = true,
        bool usePullup = true,
        uint32_t debounceMs = 50,
        uint32_t longPressMs = 3000
    );

    void begin();
    void update();

    bool isPressed() const;

    bool shortPress();
    bool longPress();

private:
    bool readPressed() const;

    uint8_t pin_;

    bool activeLow_ = true;
    bool usePullup_ = true;

    uint32_t debounceMs_ = 50;
    uint32_t longPressMs_ = 3000;

    bool rawPressed_ = false;
    bool stablePressed_ = false;

    bool longPressReported_ = false;

    bool shortPressEvent_ = false;
    bool longPressEvent_ = false;

    uint32_t rawChangeMs_ = 0;
    uint32_t pressStartMs_ = 0;
};