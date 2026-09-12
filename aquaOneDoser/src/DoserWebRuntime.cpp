#include "DoserWebRuntime.h"

#include "DiagnosticsManager.h"
#include "MqttManager.h"
#include "PumpDriver.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"

#include <Arduino.h>
#include <Update.h>

DoserWebRuntime::DoserWebRuntime(
    TimeManager& timeManager, MqttManager& mqttManager,
    DiagnosticsManager& diagnosticsManager, SchedulerManager& schedulerManager,
    WiFiManager& wifiManager, PumpDriver& pumpDriver
)
    : time_(timeManager),
      mqtt_(mqttManager),
      diagnostics_(diagnosticsManager),
      scheduler_(schedulerManager),
      wifi_(wifiManager),
      driver_(pumpDriver) {
}

unsigned long DoserWebRuntime::nowMs() const {
    return millis();
}

size_t DoserWebRuntime::availableFirmwareSpace() const {
    return ESP.getFreeSketchSpace();
}

bool DoserWebRuntime::beginFirmwareUpdate(size_t expectedSize) {
    return Update.begin(expectedSize, U_FLASH);
}

size_t DoserWebRuntime::writeFirmware(const uint8_t* data, size_t length) {
    return Update.write(const_cast<uint8_t*>(data), length);
}

bool DoserWebRuntime::endFirmwareUpdate() {
    return Update.end(true);
}

void DoserWebRuntime::abortFirmwareUpdate() {
    Update.abort();
}

const char* DoserWebRuntime::firmwareError() const {
    return Update.errorString();
}

void DoserWebRuntime::stopPumps() {
    driver_.stopAll();
}

void DoserWebRuntime::setOtaInProgress(bool inProgress) {
    scheduler_.setOtaInProgress(inProgress);
}

void DoserWebRuntime::serviceDuringUpload() {
    driver_.loop();
    wifi_.loop();
    time_.loop();
    scheduler_.loop();
    diagnostics_.loop();
    mqtt_.loop();
    yield();
}

void DoserWebRuntime::restartDevice() {
    ESP.restart();
}
