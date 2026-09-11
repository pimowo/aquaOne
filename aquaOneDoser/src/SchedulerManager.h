#pragma once

#include <Arduino.h>
#include <time.h>

#include "PumpManager.h"
#include "TimeManager.h"

class PumpDriver;

#ifndef SCHEDULER_TEST_MODE
#define SCHEDULER_TEST_MODE 0
#endif

class SchedulerManager {
public:
    static constexpr uint8_t SCHEDULE_WINDOW_MINUTES = 5;
    static constexpr unsigned long PUMP_GAP_MS = 2000UL;

    struct DateResult {
        bool valid = false;
        uint32_t timestamp = 0;
    };

    bool begin(PumpManager& pumpManager, TimeManager& timeManager, PumpDriver& pumpDriver);
    void loop();

    static uint8_t weekdayBitFromTmWday(int tmWday);
    static uint32_t dateKey(const tm& localTime);
    static uint32_t calculateRemainingDoses(const PumpConfig& pump);

    DateResult getNextDose(size_t pumpIndex) const;
    DateResult getEstimatedLastDose(size_t pumpIndex) const;
    void printNextDoses() const;
    bool isAutomaticDosingActive() const;
    const char* getSuspensionReason() const;
    void setOtaInProgress(bool inProgress);
    bool isOtaInProgress() const;

private:
    enum class State : uint8_t { IDLE, RUNNING_PUMP, WAITING_GAP };

    PumpManager* pumps = nullptr;
    TimeManager* time = nullptr;
    PumpDriver* driver = nullptr;
    State state = State::IDLE;
    unsigned long gapStartedAt = 0;
    unsigned long pumpStartedAt = 0;
    unsigned long plannedRuntimeMs = 0;
    int8_t activePump = -1;
    uint32_t activeEventDate = 0;
    bool timeWarningLogged = false;
    bool automaticDosingActive = false;
    const char* suspensionReason = "Inicjalizacja";
    bool otaInProgress = false;
    bool otaWarningLogged = false;
    uint32_t calibrationLogDate[PUMP_COUNT]{};
    uint32_t liquidLogDate[PUMP_COUNT]{};
    uint32_t runtimeLogDate[PUMP_COUNT]{};
    uint32_t invalidConfigLogDate[PUMP_COUNT]{};

#if SCHEDULER_TEST_MODE
    PumpConfig testPumps[PUMP_COUNT]{};
#endif

    const PumpConfig& pumpAt(size_t index) const;
    PumpConfig& mutablePumpAt(size_t index);
    bool getReliableLocalTime(tm& local, time_t& utcTimestamp) const;
    bool findDueEvent(const PumpConfig& pump, const tm& nowLocal,
                      time_t nowTimestamp, uint32_t& eventDate) const;
    bool processPump(size_t index, const tm& nowLocal, time_t nowTimestamp);
    void detectMissedDoses(const tm& nowLocal, time_t nowTimestamp);
    void serviceRunningPump();
    void finishSuccessfulDose(size_t index, uint32_t eventDate);
    void enterGap();
    static bool scheduledOn(const PumpConfig& pump, const tm& local);
};
