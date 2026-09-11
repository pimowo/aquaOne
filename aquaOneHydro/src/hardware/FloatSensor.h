#pragma once

#include <Arduino.h>

class FloatSensor
{
public:
    explicit FloatSensor(uint8_t pin);

    void configure(
        bool activeLow,
        bool usePullup,
        uint32_t debounceMs
    );

    void begin();
    void update();

    bool isActive() const;
    bool rawActive() const;

private:
    bool readActive() const;

    uint8_t pin_;

    bool activeLow_ = true;
    bool usePullup_ = true;
    uint32_t debounceMs_ = 100;

    bool rawState_ = false;
    bool stableState_ = false;

    uint32_t lastChangeMs_ = 0;
};