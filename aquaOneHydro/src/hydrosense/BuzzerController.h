#pragma once

#include <Arduino.h>

#include "hardware/Buzzer.h"
#include "hydrosense/AlarmManager.h"

class BuzzerController
{
public:
    BuzzerController(
        Buzzer& buzzer,
        AlarmManager& alarmManager
    );

    void begin();
    void update();

    void mute();
    bool isMuted() const;

private:
    enum class PatternState : uint8_t
    {
        Idle,
        BeepOn,
        BeepOff,
        Pause
    };

    void resetPattern();

    void updateWarning(
        uint32_t now
    );

    void updateCritical(
        uint32_t now
    );

    Buzzer& buzzer_;
    AlarmManager& alarmManager_;

    PatternState patternState_ =
        PatternState::Idle;

    uint32_t stateStartMs_ = 0;

    uint8_t beepCount_ = 0;

    bool muted_ = false;

    AlarmManager::Code lastAlarmCode_ =
        AlarmManager::Code::None;
};