#include "WaterReserve.h"

WaterReserve::WaterReserve(
    WaterTank& waterTank
)
    : waterTank_(waterTank)
{
}

void WaterReserve::configure(
    float lowLevelPercent,
    float criticalLevelPercent,
    float hysteresisPercent
)
{
    lowLevelPercent_ =
        lowLevelPercent;

    criticalLevelPercent_ =
        criticalLevelPercent;

    hysteresisPercent_ =
        hysteresisPercent;
}

void WaterReserve::begin()
{
    state_ = State::Unknown;
}

void WaterReserve::update()
{
    /*
     * Brak wiarygodnego pomiaru oznacza Unknown.
     *
     * Na starcie jest to celowe:
     * pompa nie dostanie zgody dopóki nie znamy
     * poziomu w zbiorniku RO.
     */
    if (!waterTank_.isValid())
    {
        state_ = State::Unknown;
        return;
    }

    const float level =
        waterTank_.levelPercent();

    switch (state_)
    {
        case State::Unknown:
        {
            if (level <= criticalLevelPercent_)
            {
                state_ = State::Critical;
            }
            else if (level <= lowLevelPercent_)
            {
                state_ = State::Low;
            }
            else
            {
                state_ = State::Ok;
            }

            break;
        }

        case State::Ok:
        {
            if (level <= criticalLevelPercent_)
            {
                state_ = State::Critical;
            }
            else if (level <= lowLevelPercent_)
            {
                state_ = State::Low;
            }

            break;
        }

        case State::Low:
        {
            if (level <= criticalLevelPercent_)
            {
                state_ = State::Critical;
            }
            else if (
                level >=
                lowLevelPercent_ +
                hysteresisPercent_
            )
            {
                state_ = State::Ok;
            }

            break;
        }

        case State::Critical:
        {
            if (
                level >=
                criticalLevelPercent_ +
                hysteresisPercent_
            )
            {
                if (level <= lowLevelPercent_)
                {
                    state_ = State::Low;
                }
                else
                {
                    state_ = State::Ok;
                }
            }

            break;
        }
    }
}

WaterReserve::State
WaterReserve::state() const
{
    return state_;
}

bool WaterReserve::allowsPump() const
{
    return
        state_ == State::Ok ||
        state_ == State::Low;
}

bool WaterReserve::isLow() const
{
    return
        state_ == State::Low;
}

bool WaterReserve::isCritical() const
{
    return
        state_ == State::Critical;
}

bool WaterReserve::isUnknown() const
{
    return
        state_ == State::Unknown;
}