#include "TopupController.h"

TopupController::TopupController(
    Pump& pump,
    FloatSensor& floatSensor
)
    : pump_(pump),
      floatSensor_(floatSensor)
{
}

void TopupController::configure(
    uint32_t startDelayMs,
    uint32_t maxPumpRuntimeMs
)
{
    startDelayMs_ =
        startDelayMs;

    maxPumpRuntimeMs_ =
        maxPumpRuntimeMs;
}

void TopupController::begin()
{
    pump_.off();

    serviceMode_ = false;
    pumpAllowed_ = false;

    state_ = State::Idle;
    stateStartMs_ = millis();
}

void TopupController::update()
{
    floatSensor_.update();

    /*
     * SERVICE ma najwyższy priorytet.
     */
    if (serviceMode_)
    {
        pump_.off();
        return;
    }

    /*
     * Zewnętrzna blokada bezpieczeństwa.
     *
     * Może pochodzić np. z:
     * - pustego zbiornika RO,
     * - braku pomiaru zbiornika,
     * - błędu czujnika.
     */
    if (!pumpAllowed_)
    {
        pump_.off();

        if (state_ != State::Lockout)
        {
            if (state_ != State::Blocked)
            {
                enterState(
                    State::Blocked
                );
            }
        }

        return;
    }

    /*
     * Gdy warunki bezpieczeństwa
     * wrócą do normy, automat rozpoczyna
     * ocenę od początku.
     */
    if (state_ == State::Blocked)
    {
        enterState(
            State::Idle
        );
    }

    const uint32_t now =
        millis();

    const bool lowWater =
        floatSensor_.isActive();

    switch (state_)
    {
        case State::Idle:
        {
            pump_.off();

            if (lowWater)
            {
                enterState(
                    State::Waiting
                );
            }

            break;
        }

        case State::Waiting:
        {
            pump_.off();

            if (!lowWater)
            {
                enterState(
                    State::Idle
                );

                break;
            }

            if (
                (now - stateStartMs_) >=
                startDelayMs_
            )
            {
                enterState(
                    State::Pumping
                );
            }

            break;
        }

        case State::Pumping:
        {
            if (!lowWater)
            {
                enterState(
                    State::Idle
                );

                break;
            }

            if (
                (now - stateStartMs_) >=
                maxPumpRuntimeMs_
            )
            {
                enterState(
                    State::Lockout
                );
            }

            break;
        }

        case State::Blocked:
        {
            pump_.off();
            break;
        }

        case State::Lockout:
        {
            pump_.off();
            break;
        }
    }
}

void TopupController::setServiceMode(
    bool enabled
)
{
    if (serviceMode_ == enabled)
    {
        return;
    }

    serviceMode_ = enabled;

    pump_.off();

    if (state_ != State::Lockout)
    {
        enterState(
            pumpAllowed_
                ? State::Idle
                : State::Blocked
        );
    }
}

bool TopupController::
isServiceMode() const
{
    return serviceMode_;
}

void TopupController::setPumpAllowed(
    bool allowed
)
{
    if (pumpAllowed_ == allowed)
    {
        return;
    }

    pumpAllowed_ = allowed;

    /*
     * Odebranie zgody ma działać
     * natychmiast.
     */
    if (!pumpAllowed_)
    {
        pump_.off();

        if (state_ != State::Lockout)
        {
            enterState(
                State::Blocked
            );
        }
    }
}

bool TopupController::
isPumpAllowed() const
{
    return pumpAllowed_;
}

TopupController::State
TopupController::state() const
{
    return state_;
}

bool TopupController::isLocked() const
{
    return
        state_ ==
        State::Lockout;
}

void TopupController::resetLockout()
{
    pump_.off();

    if (pumpAllowed_)
    {
        enterState(
            State::Idle
        );
    }
    else
    {
        enterState(
            State::Blocked
        );
    }
}

void TopupController::enterState(
    State newState
)
{
    state_ = newState;

    stateStartMs_ =
        millis();

    if (
        newState ==
            State::Pumping &&
        !serviceMode_ &&
        pumpAllowed_
    )
    {
        pump_.on();
    }
    else
    {
        pump_.off();
    }
}