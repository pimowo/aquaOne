#include "BuzzerController.h"

BuzzerController::BuzzerController(
    Buzzer& buzzer,
    AlarmManager& alarmManager
)
    : buzzer_(buzzer),
      alarmManager_(alarmManager)
{
}

void BuzzerController::begin()
{
    buzzer_.off();

    muted_ = false;

    lastAlarmCode_ =
        AlarmManager::Code::None;

    resetPattern();
}

void BuzzerController::update()
{
    const auto currentCode =
        alarmManager_.code();

    const auto currentSeverity =
        alarmManager_.severity();

    /*
     * Gdy alarm znika:
     * - resetujemy wzorzec,
     * - kasujemy wyciszenie,
     * - przygotowujemy buzzer na kolejny alarm.
     */
    if (
        currentCode ==
        AlarmManager::Code::None
    )
    {
        buzzer_.off();

        muted_ = false;

        lastAlarmCode_ =
            AlarmManager::Code::None;

        resetPattern();

        return;
    }

    /*
     * Pojawił się inny alarm.
     *
     * Nowy alarm nie dziedziczy
     * wyciszenia poprzedniego.
     */
    if (
        currentCode !=
        lastAlarmCode_
    )
    {
        muted_ = false;

        resetPattern();

        lastAlarmCode_ =
            currentCode;
    }

    if (muted_)
    {
        buzzer_.off();
        return;
    }

    const uint32_t now =
        millis();

    switch (currentSeverity)
    {
        case AlarmManager::Severity::Warning:
        {
            updateWarning(now);
            break;
        }

        case AlarmManager::Severity::Critical:
        {
            updateCritical(now);
            break;
        }

        case AlarmManager::Severity::Info:
        case AlarmManager::Severity::None:
        default:
        {
            buzzer_.off();
            resetPattern();
            break;
        }
    }
}

void BuzzerController::mute()
{
    muted_ = true;

    buzzer_.off();

    resetPattern();
}

bool BuzzerController::isMuted() const
{
    return muted_;
}

void BuzzerController::resetPattern()
{
    buzzer_.off();

    patternState_ =
        PatternState::Idle;

    stateStartMs_ =
        millis();

    beepCount_ = 0;
}

void BuzzerController::updateWarning(
    uint32_t now
)
{
    switch (patternState_)
    {
        case PatternState::Idle:
        {
            buzzer_.on();

            patternState_ =
                PatternState::BeepOn;

            stateStartMs_ = now;

            break;
        }

        case PatternState::BeepOn:
        {
            if (
                (now - stateStartMs_) >=
                150
            )
            {
                buzzer_.off();

                patternState_ =
                    PatternState::Pause;

                stateStartMs_ = now;
            }

            break;
        }

        case PatternState::Pause:
        {
            if (
                (now - stateStartMs_) >=
                5000
            )
            {
                patternState_ =
                    PatternState::Idle;
            }

            break;
        }

        case PatternState::BeepOff:
        default:
        {
            patternState_ =
                PatternState::Idle;

            break;
        }
    }
}

void BuzzerController::updateCritical(
    uint32_t now
)
{
    switch (patternState_)
    {
        case PatternState::Idle:
        {
            beepCount_ = 0;

            buzzer_.on();

            patternState_ =
                PatternState::BeepOn;

            stateStartMs_ = now;

            break;
        }

        case PatternState::BeepOn:
        {
            if (
                (now - stateStartMs_) >=
                150
            )
            {
                buzzer_.off();

                ++beepCount_;

                stateStartMs_ = now;

                if (beepCount_ >= 3)
                {
                    patternState_ =
                        PatternState::Pause;
                }
                else
                {
                    patternState_ =
                        PatternState::BeepOff;
                }
            }

            break;
        }

        case PatternState::BeepOff:
        {
            if (
                (now - stateStartMs_) >=
                150
            )
            {
                buzzer_.on();

                patternState_ =
                    PatternState::BeepOn;

                stateStartMs_ = now;
            }

            break;
        }

        case PatternState::Pause:
        {
            if (
                (now - stateStartMs_) >=
                3000
            )
            {
                patternState_ =
                    PatternState::Idle;
            }

            break;
        }
    }
}