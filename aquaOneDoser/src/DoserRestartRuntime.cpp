#include "DoserRestartRuntime.h"

#include <Arduino.h>

#include "PumpDriver.h"

uint32_t DoserRestartRuntime::nowMs() const { return millis(); }
void DoserRestartRuntime::stopPumps() { pumps_.stopAll(); }
void DoserRestartRuntime::restartDevice() { ESP.restart(); }
