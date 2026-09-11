#pragma once

#include <Arduino.h>

#include "hardware/Pump.h"
#include "hardware/FloatSensor.h"

class TopupController
{
public:
    enum class State
    {
        Idle,
        Waiting,
        Pumping,
        Blocked,
        Lockout
    };

    TopupController(
        Pump& pump,
        FloatSensor& floatSensor
    );

    void configure(
        uint32_t startDelayMs,
        uint32_t maxPumpRuntimeMs
    );

    void begin();
    void update();

    void setServiceMode(bool enabled);

    bool isServiceMode() const;

    void setPumpAllowed(bool allowed);

    bool isPumpAllowed() const;

    State state() const;

    bool isLocked() const;

    void resetLockout();

private:
    void enterState(State newState);

    Pump& pump_;
    FloatSensor& floatSensor_;

    uint32_t startDelayMs_ = 3000;

    uint32_t maxPumpRuntimeMs_ =
        120000;

    State state_ = State::Idle;

    uint32_t stateStartMs_ = 0;

    bool serviceMode_ = false;

    // Fail-safe:
    // dopóki aplikacja nie potwierdzi
    // warunków bezpieczeństwa,
    // pompa nie może pracować.
    bool pumpAllowed_ = false;
};