#pragma once

#include <Arduino.h>
#include "PumpConfig.h"

class PumpDriver {
public:
    enum class StopReason : uint8_t { NONE, NORMAL, SAFETY_TIMEOUT, EMERGENCY };

    bool begin();
    void loop();
    bool startPump(uint8_t index);
    bool stopPump(uint8_t index);
    void stopAll();
    bool isRunning(uint8_t index) const;
    bool anyRunning() const;
    int8_t getRunningPump() const;
    StopReason getLastStopReason() const;
    int8_t getLastStoppedPump() const;

private:
    static constexpr uint8_t PINS[PUMP_COUNT] = {1, 2, 4, 5, 6, 7, 10, 11};

    bool initialized = false;
    bool running[PUMP_COUNT]{};
    int8_t runningPump = -1;
    int8_t lastStoppedPump = -1;
    unsigned long startedAt = 0;
    StopReason lastStopReason = StopReason::NONE;

    bool validIndex(uint8_t index) const;
    void stopInternal(uint8_t index, StopReason reason);
};