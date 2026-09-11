#include "DiagnosticsManager.h"

#include "MqttManager.h"
#include "PumpManager.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "app_config.h"

#include <cmath>

bool DiagnosticsManager::begin(TimeManager& timeManager, PumpManager& pumpManager,
                               SchedulerManager& schedulerManager, MqttManager& mqttManager) {
    time = &timeManager;
    pumps = &pumpManager;
    scheduler = &schedulerManager;
    mqtt = &mqttManager;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        currentPumpStatus[i] = calculatePumpStatus(i);
        currentNoLiquid[i] = hasNoLiquid(i);
        currentLowLiquid[i] = hasLowLiquid(i);
        currentMissedToday[i] = hasMissedDoseToday(i);
    }
    currentSystemStatus = calculateSystemStatus();
    initialized = true;
    ++revision;
    Serial.printf("[DIAG] Status: %s\n", getSystemStatusText());
    if (currentSystemStatus == SystemStatus::WARNING && !time->isNtpSynced())
        Serial.println("[DIAG] Reason: NTP unavailable");
    return true;
}

void DiagnosticsManager::loop() {
    if (!initialized) return;
    bool changed = false;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        const PumpStatus status = calculatePumpStatus(i);
        const bool noLiquid = hasNoLiquid(i);
        const bool lowLiquid = hasLowLiquid(i);
        const bool missedToday = hasMissedDoseToday(i);

        if (status != currentPumpStatus[i]) {
            currentPumpStatus[i] = status;
            changed = true;
            switch (status) {
                case PumpStatus::MISSING_CALIBRATION:
                    Serial.printf("[SAFETY] Pump %u missing calibration\n", i + 1); break;
                case PumpStatus::NO_LIQUID:
                    Serial.printf("[SAFETY] Pump %u insufficient liquid\n", i + 1); break;
                case PumpStatus::INVALID_CONFIGURATION:
                    Serial.printf("[SAFETY] Pump %u invalid configuration\n", i + 1); break;
                default: break;
            }
        }
        if (lowLiquid != currentLowLiquid[i]) {
            currentLowLiquid[i] = lowLiquid;
            changed = true;
            if (lowLiquid) Serial.printf("[SAFETY] Pump %u low liquid\n", i + 1);
        }
        if (noLiquid != currentNoLiquid[i]) {
            currentNoLiquid[i] = noLiquid;
            changed = true;
        }
        if (missedToday != currentMissedToday[i]) {
            currentMissedToday[i] = missedToday;
            changed = true;
        }
    }

    const SystemStatus status = calculateSystemStatus();
    if (status != currentSystemStatus) {
        currentSystemStatus = status;
        changed = true;
        Serial.printf("[DIAG] Status: %s\n", getSystemStatusText());
        if (status == SystemStatus::ERROR) {
            if (time->isRtcConfigured() && !time->isRtcOk()) {
                Serial.println("[DIAG] Reason: RTC communication error");
            } else if (!time->isTimeValid()) {
                Serial.println("[DIAG] Reason: Waiting for time synchronization");
            }
        } else if (status == SystemStatus::WARNING && time->isNtpConfigured() && !time->isNtpSynced()) {
            Serial.println("[DIAG] Reason: NTP unavailable");
        }
    }
    if (changed) ++revision;
}

DiagnosticsManager::SystemStatus DiagnosticsManager::calculateSystemStatus() const {
    if (scheduler == nullptr || !scheduler->isAutomaticDosingActive())
        return SystemStatus::ERROR;

    bool warning = (time->isNtpConfigured() && !time->isNtpSynced()) || !mqtt->isConnected();
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        if (currentPumpStatus[i] != PumpStatus::PUMP_DISABLED &&
            currentPumpStatus[i] != PumpStatus::OK) warning = true;
        if (currentLowLiquid[i] || currentMissedToday[i]) warning = true;
    }
    return warning ? SystemStatus::WARNING : SystemStatus::OK;
}

DiagnosticsManager::PumpStatus DiagnosticsManager::calculatePumpStatus(size_t index) const {
    if (pumps == nullptr || index >= PUMP_COUNT)
        return PumpStatus::INVALID_CONFIGURATION;

    const PumpConfig& pump = pumps->getPump(index);
    if (!pump.enabled) return PumpStatus::PUMP_DISABLED;

    if (!std::isfinite(pump.doseMl) || !std::isfinite(pump.remainingMl) ||
        pump.doseMl <= 0.0F || pump.doseMl > MAX_SINGLE_DOSE_ML ||
        pump.remainingMl < 0.0F || pump.remainingMl > MAX_REMAINING_ML)
        return PumpStatus::INVALID_CONFIGURATION;

    if (!std::isfinite(pump.calibrationMlPerSec) || pump.calibrationMlPerSec <= 0.0F)
        return PumpStatus::MISSING_CALIBRATION;

    const float runtimeSec = pump.doseMl / pump.calibrationMlPerSec;
    if (!std::isfinite(runtimeSec) || runtimeSec > MAX_PUMP_RUNTIME_SEC)
        return PumpStatus::INVALID_CONFIGURATION;

    if (pump.remainingMl < pump.doseMl) return PumpStatus::NO_LIQUID;
    return PumpStatus::OK;
}

DiagnosticsManager::SystemStatus DiagnosticsManager::getSystemStatus() const {
    return currentSystemStatus;
}
const char* DiagnosticsManager::getSystemStatusText() const {
    return systemStatusText(currentSystemStatus);
}
bool DiagnosticsManager::isAutomaticDosingActive() const {
    return scheduler != nullptr && scheduler->isAutomaticDosingActive();
}
const char* DiagnosticsManager::getSuspensionReason() const {
    return scheduler == nullptr ? "Blad schedulera" : scheduler->getSuspensionReason();
}
DiagnosticsManager::PumpStatus DiagnosticsManager::getPumpStatus(size_t index) const {
    return index < PUMP_COUNT ? currentPumpStatus[index] : PumpStatus::INVALID_CONFIGURATION;
}
const char* DiagnosticsManager::getPumpStatusText(size_t index) const {
    return pumpStatusText(getPumpStatus(index));
}
bool DiagnosticsManager::hasNoLiquid(size_t index) const {
    if (pumps == nullptr || index >= PUMP_COUNT) return false;
    const PumpConfig& pump = pumps->getPump(index);
    return pump.enabled && pump.doseMl > 0.0F && pump.remainingMl < pump.doseMl;
}
bool DiagnosticsManager::hasLowLiquid(size_t index) const {
    if (pumps == nullptr || index >= PUMP_COUNT) return false;
    const PumpConfig& pump = pumps->getPump(index);
    const uint32_t doses = SchedulerManager::calculateRemainingDoses(pump);
    return pump.enabled && doses > 0 && doses <= LOW_REMAINING_DOSES;
}
bool DiagnosticsManager::hasMissedDoseToday(size_t index) const {
    if (pumps == nullptr || index >= PUMP_COUNT) return false;
    const uint32_t today = currentLocalDate();
    return today != 0 && pumps->getPump(index).lastMissedDoseDate == today;
}
uint32_t DiagnosticsManager::getLastMissedDoseDate(size_t index) const {
    return pumps != nullptr && index < PUMP_COUNT
        ? pumps->getPump(index).lastMissedDoseDate : 0;
}
uint32_t DiagnosticsManager::getRevision() const { return revision; }

uint32_t DiagnosticsManager::currentLocalDate() const {
    if (time == nullptr || !time->isRtcOk()) return 0;
    const tm local = time->getLocalTime();
    if (local.tm_year + 1900 < 2023) return 0;
    return static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
}

const char* DiagnosticsManager::systemStatusText(SystemStatus status) {
    switch (status) {
        case SystemStatus::OK: return "OK";
        case SystemStatus::WARNING: return "WARNING";
        default: return "ERROR";
    }
}
const char* DiagnosticsManager::pumpStatusText(PumpStatus status) {
    switch (status) {
        case PumpStatus::OK: return "OK";
        case PumpStatus::PUMP_DISABLED: return "WYLACZONA";
        case PumpStatus::MISSING_CALIBRATION: return "BRAK KALIBRACJI";
        case PumpStatus::NO_LIQUID: return "BRAK PLYNU";
        default: return "BLEDNA KONFIGURACJA";
    }
}
