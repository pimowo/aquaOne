#pragma once

#include <Arduino.h>

#include "hydrosense/WaterTank.h"
#include "hydrosense/WaterReserve.h"
#include "hydrosense/TopupController.h"

class AlarmManager
{
public:
    enum class Severity : uint8_t
    {
        None = 0,
        Info,
        Warning,
        Critical
    };

    enum class Code : uint8_t
    {
        None = 0,

        TankSensorFault,
        TankLevelLow,
        TankLevelCritical,
        PumpLockout
    };

    AlarmManager(
        WaterTank& waterTank,
        WaterReserve& waterReserve,
        TopupController& topupController
    );

    void begin();
    void update();

    bool hasAlarm() const;

    Code code() const;
    Severity severity() const;

private:
    void setAlarm(
        Code code,
        Severity severity
    );

    void clearAlarm();

    WaterTank& waterTank_;
    WaterReserve& waterReserve_;

    TopupController& topupController_;

    Code code_ = Code::None;

    Severity severity_ =
        Severity::None;
};