#pragma once

#include <Arduino.h>

#include "hydrosense/WaterTank.h"

class WaterReserve
{
public:
    enum class State : uint8_t
    {
        Unknown = 0,
        Ok,
        Low,
        Critical
    };

    explicit WaterReserve(
        WaterTank& waterTank
    );

    void configure(
        float lowLevelPercent,
        float criticalLevelPercent,
        float hysteresisPercent
    );

    void begin();
    void update();

    State state() const;

    bool allowsPump() const;
    bool isLow() const;
    bool isCritical() const;
    bool isUnknown() const;

private:
    WaterTank& waterTank_;

    float lowLevelPercent_ = 25.0f;
    float criticalLevelPercent_ = 10.0f;
    float hysteresisPercent_ = 3.0f;

    State state_ = State::Unknown;
};