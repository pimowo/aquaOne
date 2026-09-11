#pragma once

#include <Arduino.h>

#include "PumpConfig.h"

class MqttManager;
class PumpManager;
class SchedulerManager;
class TimeManager;

class DiagnosticsManager {
public:
    enum class SystemStatus : uint8_t { OK, WARNING, ERROR };
    enum class PumpStatus : uint8_t {
        OK,
        PUMP_DISABLED,
        MISSING_CALIBRATION,
        NO_LIQUID,
        INVALID_CONFIGURATION
    };

    bool begin(TimeManager& timeManager, PumpManager& pumpManager,
               SchedulerManager& schedulerManager, MqttManager& mqttManager);
    void loop();

    SystemStatus getSystemStatus() const;
    const char* getSystemStatusText() const;
    bool isAutomaticDosingActive() const;
    const char* getSuspensionReason() const;

    PumpStatus getPumpStatus(size_t index) const;
    const char* getPumpStatusText(size_t index) const;
    bool hasNoLiquid(size_t index) const;
    bool hasLowLiquid(size_t index) const;
    bool hasMissedDoseToday(size_t index) const;
    uint32_t getLastMissedDoseDate(size_t index) const;
    uint32_t getRevision() const;

private:
    TimeManager* time = nullptr;
    PumpManager* pumps = nullptr;
    SchedulerManager* scheduler = nullptr;
    MqttManager* mqtt = nullptr;

    SystemStatus currentSystemStatus = SystemStatus::ERROR;
    PumpStatus currentPumpStatus[PUMP_COUNT]{};
    bool currentNoLiquid[PUMP_COUNT]{};
    bool currentLowLiquid[PUMP_COUNT]{};
    bool currentMissedToday[PUMP_COUNT]{};
    uint32_t revision = 0;
    bool initialized = false;

    SystemStatus calculateSystemStatus() const;
    PumpStatus calculatePumpStatus(size_t index) const;
    uint32_t currentLocalDate() const;
    static const char* systemStatusText(SystemStatus status);
    static const char* pumpStatusText(PumpStatus status);
};
