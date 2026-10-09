#include "DoserOtaRuntime.h"

#include "PumpDriver.h"
#include "SchedulerManager.h"

#include <Arduino.h>
#include <Update.h>

uint32_t DoserOtaRuntime::nowMs() const { return millis(); }
size_t DoserOtaRuntime::availableFirmwareSpace() const { return ESP.getFreeSketchSpace(); }
bool DoserOtaRuntime::beginFirmwareUpdate(size_t size) { return Update.begin(size, U_FLASH); }
size_t DoserOtaRuntime::writeFirmware(const uint8_t* data, size_t length) {
    return Update.write(const_cast<uint8_t*>(data), length);
}
bool DoserOtaRuntime::endFirmwareUpdate() { return Update.end(true); }
void DoserOtaRuntime::abortFirmwareUpdate() { Update.abort(); }
const char* DoserOtaRuntime::firmwareError() const { return Update.errorString(); }
void DoserOtaRuntime::stopPumps() { pumps_.stopAll(); }
void DoserOtaRuntime::setOtaInProgress(bool active) { scheduler_.setOtaInProgress(active); }
