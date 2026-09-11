#include "AlarmManager.h"

AlarmManager::AlarmManager(
    WaterTank& waterTank,
    WaterReserve& waterReserve,
    TopupController& topupController
)
    : waterTank_(waterTank),
      waterReserve_(waterReserve),
      topupController_(topupController)
{
}

void AlarmManager::begin()
{
    clearAlarm();
}

void AlarmManager::update()
{
    /*
     * Priorytet alarmów:
     *
     * 1. trwały LOCKOUT pompy,
     * 2. błąd czujnika zbiornika,
     * 3. krytycznie niski poziom,
     * 4. niski poziom.
     */

    if (topupController_.isLocked())
    {
        setAlarm(
            Code::PumpLockout,
            Severity::Critical
        );

        return;
    }

    if (waterTank_.hasSensorFault())
    {
        setAlarm(
            Code::TankSensorFault,
            Severity::Critical
        );

        return;
    }

    if (waterReserve_.isCritical())
    {
        setAlarm(
            Code::TankLevelCritical,
            Severity::Critical
        );

        return;
    }

    if (waterReserve_.isLow())
    {
        setAlarm(
            Code::TankLevelLow,
            Severity::Warning
        );

        return;
    }

    clearAlarm();
}

bool AlarmManager::hasAlarm() const
{
    return code_ != Code::None;
}

AlarmManager::Code
AlarmManager::code() const
{
    return code_;
}

AlarmManager::Severity
AlarmManager::severity() const
{
    return severity_;
}

void AlarmManager::setAlarm(
    Code code,
    Severity severity
)
{
    code_ = code;
    severity_ = severity;
}

void AlarmManager::clearAlarm()
{
    code_ = Code::None;

    severity_ =
        Severity::None;
}