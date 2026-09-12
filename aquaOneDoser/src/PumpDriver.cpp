#include "PumpDriver.h"

#include "app_config.h"

constexpr uint8_t PumpDriver::PINS[PUMP_COUNT];

bool PumpDriver::begin() {
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        pinMode(PINS[i], OUTPUT);
        digitalWrite(PINS[i], LOW);
        running[i] = false;
    }
    runningPump = -1;
    lastStoppedPump = -1;
    lastStopReason = StopReason::NONE;
    initialized = true;
    stopAll();
    Serial.println("[PUMPS] GPIO initialized - all pumps OFF");
    return true;
}

void PumpDriver::loop() {
    if (runningPump < 0) return;
    const unsigned long maximumMs = static_cast<unsigned long>(MAX_PUMP_RUNTIME_SEC * 1000.0F);
    if (millis() - startedAt > maximumMs) {
        const uint8_t index = static_cast<uint8_t>(runningPump);
        stopInternal(index, StopReason::SAFETY_TIMEOUT);
        Serial.printf("[PUMPS] SAFETY STOP: Pump %u maximum runtime exceeded\n", index + 1);
    }
}

bool PumpDriver::startPump(uint8_t index) {
    if (!initialized || !validIndex(index)) {
        Serial.printf("[PUMPS] Start rejected: invalid pump index %u\n", index);
        return false;
    }
    if (runningPump >= 0) {
        Serial.println("[PUMPS] Start rejected: another pump is running");
        return false;
    }
    digitalWrite(PINS[index], HIGH);
    running[index] = true;
    runningPump = static_cast<int8_t>(index);
    startedAt = millis();
    lastStopReason = StopReason::NONE;
    lastStoppedPump = -1;
    Serial.printf("[PUMPS] Pump %u ON\n", index + 1);
    return true;
}

bool PumpDriver::stopPump(uint8_t index) {
    if (!initialized || !validIndex(index)) {
        Serial.printf("[PUMPS] Stop rejected: invalid pump index %u\n", index);
        return false;
    }
    if (!running[index]) return false;
    stopInternal(index, StopReason::NORMAL);
    return true;
}

void PumpDriver::stopAll() {
    const bool hadRunningPump = runningPump >= 0;
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        digitalWrite(PINS[i], LOW);
        running[i] = false;
    }
    if (hadRunningPump) {
        lastStoppedPump = runningPump;
        lastStopReason = StopReason::EMERGENCY;
        Serial.println("[PUMPS] Emergency stop - all pumps OFF");
    }
    runningPump = -1;
}

void PumpDriver::stopInternal(uint8_t index, StopReason reason) {
    digitalWrite(PINS[index], LOW);
    running[index] = false;
    runningPump = -1;
    lastStoppedPump = static_cast<int8_t>(index);
    lastStopReason = reason;
    Serial.printf("[PUMPS] Pump %u OFF\n", index + 1);
    Serial.printf("[PUMPS] Pump %u runtime: %lu ms\n", index + 1, millis() - startedAt);
}

bool PumpDriver::validIndex(uint8_t index) const { return index < PUMP_COUNT; }
bool PumpDriver::isRunning(uint8_t index) const { return validIndex(index) && running[index]; }
bool PumpDriver::anyRunning() const { return runningPump >= 0; }
int8_t PumpDriver::getRunningPump() const { return runningPump; }
PumpDriver::StopReason PumpDriver::getLastStopReason() const { return lastStopReason; }
int8_t PumpDriver::getLastStoppedPump() const { return lastStoppedPump; }