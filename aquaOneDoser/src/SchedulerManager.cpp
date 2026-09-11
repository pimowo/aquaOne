#include "SchedulerManager.h"
#include "PumpDriver.h"
#include "app_config.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr uint32_t MIN_VALID_TIMESTAMP = 1700000000UL;
constexpr uint32_t MAX_ESTIMATE_DAYS = 36525UL;
}

bool SchedulerManager::begin(PumpManager& pumpManager, TimeManager& timeManager, PumpDriver& pumpDriver) {
    pumps = &pumpManager;
    time = &timeManager;
    state = State::IDLE;
    Serial.println("[SCHEDULER] Start");

#if SCHEDULER_TEST_MODE
    for (size_t i = 0; i < PUMP_COUNT; ++i) testPumps[i] = pumps->getPump(i);

    tm local{};
    time_t timestamp = 0;
    if (getReliableLocalTime(local, timestamp)) {
        PumpConfig& test = testPumps[0];
        test.enabled = true;
        if (test.calibrationMlPerSec <= 0.0F) test.calibrationMlPerSec = 1.0F;
        if (test.doseMl <= 0.0F) test.doseMl = 1.0F;
        if (test.remainingMl < test.doseMl) test.remainingMl = test.doseMl;
        test.hour = static_cast<uint8_t>(local.tm_hour);
        test.minute = static_cast<uint8_t>(local.tm_min);
        test.daysMask |= weekdayBitFromTmWday(local.tm_wday);
        test.lastDoseDate = 0;
        Serial.println("[SCHEDULER] TEST MODE - kopia konfiguracji, NVS bez zmian");
    }
#endif

    tm initialLocal{};
    time_t initialTimestamp = 0;
    automaticDosingActive = getReliableLocalTime(initialLocal, initialTimestamp);
    suspensionReason = automaticDosingActive ? "None" : (time->isTimeValid() ? "TIME_INVALID" : (time->isRtcConfigured() ? "RTC_ERROR" : "NO_TIME_SOURCE"));
    if (automaticDosingActive) {
        Serial.println("[SCHEDULER] Scheduler aktywny");
    } else if (time->isRtcConfigured()) {
        Serial.println("[SCHEDULER] Automatic dosing suspended: RTC communication error");
    } else {
        Serial.println("[SCHEDULER] Automatic dosing suspended: waiting for time synchronization");
    }
    return true;
}

void SchedulerManager::loop() {
    if (pumps == nullptr || time == nullptr || driver == nullptr) return;

    if (state == State::RUNNING_PUMP) {
        serviceRunningPump();
        if (state == State::RUNNING_PUMP) return;
    }

    if (otaInProgress) {
        automaticDosingActive = false;
        suspensionReason = "OTA_IN_PROGRESS";
        if (!otaWarningLogged) {
            Serial.println("[SCHEDULER] Automatic dosing suspended: OTA in progress");
            otaWarningLogged = true;
        }
        if (driver->anyRunning()) driver->stopAll();
        return;
    }
    if (otaWarningLogged) {
        Serial.println("[SCHEDULER] OTA finished - scheduler resumed");
        otaWarningLogged = false;
    }

    tm local{};
    time_t timestamp = 0;
    if (!getReliableLocalTime(local, timestamp)) {
        if (driver->anyRunning()) driver->stopAll();
        automaticDosingActive = false;
        suspensionReason = time->isTimeValid() ? "TIME_INVALID" : (time->isRtcConfigured() ? "RTC_ERROR" : "NO_TIME_SOURCE");
        if (!timeWarningLogged) {
            if (time->isTimeValid()) {
                Serial.println("[SCHEDULER] Automatic dosing suspended: invalid time");
            } else if (time->isRtcConfigured()) {
                Serial.println("[SCHEDULER] Automatic dosing suspended: RTC communication error");
            } else {
                Serial.println("[SCHEDULER] Automatic dosing suspended: waiting for time synchronization");
            }
            timeWarningLogged = true;
        }
        return;
    }
    automaticDosingActive = true;
    suspensionReason = "None";
    detectMissedDoses(local, timestamp);

    if (timeWarningLogged) {
        Serial.println("[SCHEDULER] Wiarygodny czas odzyskany - automatyczne dozowanie wznowione");
        timeWarningLogged = false;
    }

    if (state == State::WAITING_GAP) {
        if (millis() - gapStartedAt < PUMP_GAP_MS) return;
        state = State::IDLE;
    }

    for (size_t i = 0; i < pumps->getPumpCount(); ++i) {
        if (processPump(i, local, timestamp)) break;
    }
}

uint8_t SchedulerManager::weekdayBitFromTmWday(int tmWday) {
    if (tmWday < 0 || tmWday > 6) return 0;
    const uint8_t mondayBased = static_cast<uint8_t>((tmWday + 6) % 7);
    return static_cast<uint8_t>(1U << mondayBased);
}

uint32_t SchedulerManager::dateKey(const tm& localTime) {
    return static_cast<uint32_t>(localTime.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(localTime.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(localTime.tm_mday);
}

uint32_t SchedulerManager::calculateRemainingDoses(const PumpConfig& pump) {
    if (!std::isfinite(pump.remainingMl) || !std::isfinite(pump.doseMl) ||
        pump.remainingMl < 0.0F || pump.doseMl <= 0.0F) return 0;

    const double doses = floor(static_cast<double>(pump.remainingMl) / pump.doseMl);
    if (doses >= static_cast<double>(std::numeric_limits<uint32_t>::max()))
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(doses);
}

const PumpConfig& SchedulerManager::pumpAt(size_t index) const {
#if SCHEDULER_TEST_MODE
    return testPumps[index];
#else
    return pumps->getPump(index);
#endif
}

PumpConfig& SchedulerManager::mutablePumpAt(size_t index) {
#if SCHEDULER_TEST_MODE
    return testPumps[index];
#else
    return pumps->getPump(index);
#endif
}

bool SchedulerManager::getReliableLocalTime(tm& local, time_t& utcTimestamp) const {
    if (time == nullptr || !time->isTimeValid()) return false;
    utcTimestamp = static_cast<time_t>(time->getUtcTimestamp());
    if (utcTimestamp < static_cast<time_t>(MIN_VALID_TIMESTAMP)) return false;
    local = time->getLocalTime();
    return local.tm_year + 1900 >= 2023;
}

bool SchedulerManager::scheduledOn(const PumpConfig& pump, const tm& local) {
    return (pump.daysMask & weekdayBitFromTmWday(local.tm_wday)) != 0;
}

bool SchedulerManager::findDueEvent(const PumpConfig& pump, const tm& nowLocal,
                                    time_t nowTimestamp, uint32_t& eventDate) const {
    const long windowSeconds = static_cast<long>(SCHEDULE_WINDOW_MINUTES) * 60L;

    for (int dayOffset = 0; dayOffset >= -1; --dayOffset) {
        tm candidate = nowLocal;
        candidate.tm_mday += dayOffset;
        candidate.tm_hour = pump.hour;
        candidate.tm_min = pump.minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        const time_t scheduled = mktime(&candidate);
        if (scheduled == static_cast<time_t>(-1)) continue;

        tm normalized{};
        localtime_r(&scheduled, &normalized);
        if (!scheduledOn(pump, normalized)) continue;

        const double elapsed = difftime(nowTimestamp, scheduled);
        if (elapsed >= 0.0 && elapsed < windowSeconds) {
            eventDate = dateKey(normalized);
            return true;
        }
    }
    return false;
}

void SchedulerManager::detectMissedDoses(const tm& nowLocal, time_t nowTimestamp) {
    const long windowSeconds = static_cast<long>(SCHEDULE_WINDOW_MINUTES) * 60L;
    const uint32_t today = dateKey(nowLocal);
    if (today < 20230101UL) return;

    for (size_t index = 0; index < pumps->getPumpCount(); ++index) {
        PumpConfig& pump = mutablePumpAt(index);
        if (!pump.enabled || !std::isfinite(pump.doseMl) || pump.doseMl <= 0.0F ||
            pump.doseMl > MAX_SINGLE_DOSE_ML || pump.hour > 23 || pump.minute > 59 ||
            pump.daysMask == 0 || !scheduledOn(pump, nowLocal)) continue;

        tm candidate = nowLocal;
        candidate.tm_hour = pump.hour;
        candidate.tm_min = pump.minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        const time_t scheduled = mktime(&candidate);
        if (scheduled == static_cast<time_t>(-1) ||
            difftime(nowTimestamp, scheduled) < windowSeconds ||
            pump.lastDoseDate == today || pump.lastMissedDoseDate == today) continue;

        pump.lastMissedDoseDate = today;
#if SCHEDULER_TEST_MODE
        Serial.printf("[SCHEDULER] Pump %u missed dose (TEST MODE)\n", index + 1);
#else
        Serial.printf("[SCHEDULER] Pump %u missed dose\n", index + 1);
        if (!pumps->savePump(index))
            Serial.println("[SCHEDULER] BLAD zapisu pominietej dawki do NVS");
#endif
    }
}
bool SchedulerManager::processPump(size_t index, const tm& nowLocal, time_t nowTimestamp) {
    const PumpConfig& pump = pumpAt(index);
    if (!pump.enabled || pump.doseMl <= 0.0F || pump.daysMask == 0) return false;

    uint32_t eventDate = 0;
    if (!findDueEvent(pump, nowLocal, nowTimestamp, eventDate)) return false;
    if (pump.lastDoseDate == eventDate) return false;

    if (!(pump.doseMl > 0.0F) || pump.doseMl > MAX_SINGLE_DOSE_ML ||
        !std::isfinite(pump.doseMl) || !std::isfinite(pump.remainingMl) ||
        pump.remainingMl < 0.0F || pump.remainingMl > MAX_REMAINING_ML) {
        if (invalidConfigLogDate[index] != eventDate) {
            Serial.printf("[SAFETY] Pump %u invalid configuration\n", index + 1);
            invalidConfigLogDate[index] = eventDate;
        }
        return false;
    }

    if (pump.calibrationMlPerSec <= 0.0F) {
        if (calibrationLogDate[index] != eventDate) {
            Serial.printf("[SCHEDULER] Pompa %u \"%s\" - brak kalibracji\n",
                          static_cast<unsigned>(index + 1), pump.name);
            calibrationLogDate[index] = eventDate;
        }
        return false;
    }

    if (pump.remainingMl < pump.doseMl) {
        if (liquidLogDate[index] != eventDate) {
            Serial.printf("[SCHEDULER] Pompa %u \"%s\" - brak wystarczajacej ilosci plynu\n",
                          static_cast<unsigned>(index + 1), pump.name);
            liquidLogDate[index] = eventDate;
        }
        return false;
    }

    const float runtimeSec = pump.doseMl / pump.calibrationMlPerSec;
    if (!std::isfinite(runtimeSec) || runtimeSec > MAX_PUMP_RUNTIME_SEC) {
        if (runtimeLogDate[index] != eventDate) {
            Serial.printf("[SAFETY] Pump %u runtime exceeds limit\n", index + 1);
            runtimeLogDate[index] = eventDate;
        }
        return false;
    }

    const unsigned long runtimeMs = static_cast<unsigned long>(ceil(runtimeSec * 1000.0F));
    if (runtimeMs == 0 || !driver->startPump(static_cast<uint8_t>(index))) return false;

    activePump = static_cast<int8_t>(index);
    activeEventDate = eventDate;
    pumpStartedAt = millis();
    plannedRuntimeMs = runtimeMs;
    state = State::RUNNING_PUMP;
    Serial.printf("[SCHEDULER] Pompa %u \"%s\" - start, dawka %.2f ml, czas %lu ms\n",
                  static_cast<unsigned>(index + 1), pump.name, pump.doseMl, runtimeMs);
    return true;
}

void SchedulerManager::serviceRunningPump() {
    if (activePump < 0 || activePump >= static_cast<int8_t>(PUMP_COUNT)) {
        if (driver->anyRunning()) driver->stopAll();
        Serial.println("[SAFETY] Nieprawidlowy stan aktywnej pompy");
        enterGap();
        return;
    }

    const size_t index = static_cast<size_t>(activePump);
    if (!driver->isRunning(static_cast<uint8_t>(index))) {
        Serial.printf("[SCHEDULER] Pompa %u - dawka przerwana, bez zapisu\n",
                      static_cast<unsigned>(index + 1));
        enterGap();
        return;
    }

    if (millis() - pumpStartedAt < plannedRuntimeMs) return;
    if (!driver->stopPump(static_cast<uint8_t>(index))) {
        driver->stopAll();
        Serial.printf("[SCHEDULER] Pompa %u - blad zatrzymania, bez zapisu\n",
                      static_cast<unsigned>(index + 1));
        enterGap();
        return;
    }
    finishSuccessfulDose(index, activeEventDate);
    enterGap();
}

void SchedulerManager::finishSuccessfulDose(size_t index, uint32_t eventDate) {
    PumpConfig& pump = mutablePumpAt(index);
    Serial.printf("[SCHEDULER] Pompa %u - dawka wykonana poprawnie\n",
                  static_cast<unsigned>(index + 1));

    pump.remainingMl -= pump.doseMl;
    if (pump.remainingMl < 0.0F) pump.remainingMl = 0.0F;
    pump.lastDoseDate = eventDate;

#if SCHEDULER_TEST_MODE
    Serial.println("[SCHEDULER] TEST MODE - wynik nie zostanie zapisany w NVS");
#else
    if (!pumps->savePump(index))
        Serial.println("[SCHEDULER] BLAD zapisu wyniku dawki do NVS");
#endif

    Serial.printf("[SCHEDULER] Remaining: %.2f ml\n", pump.remainingMl);
    Serial.printf("[SCHEDULER] Last dose date: %lu\n", static_cast<unsigned long>(pump.lastDoseDate));
}

void SchedulerManager::enterGap() {
    activePump = -1;
    activeEventDate = 0;
    plannedRuntimeMs = 0;
    state = State::WAITING_GAP;
    gapStartedAt = millis();
}

SchedulerManager::DateResult SchedulerManager::getNextDose(size_t pumpIndex) const {
    DateResult result;
    if (pumps == nullptr || pumpIndex >= pumps->getPumpCount()) return result;

    const PumpConfig& pump = pumpAt(pumpIndex);
    if (!pump.enabled || pump.daysMask == 0) return result;

    tm nowLocal{};
    time_t nowTimestamp = 0;
    if (!getReliableLocalTime(nowLocal, nowTimestamp)) return result;

    for (int dayOffset = 0; dayOffset <= 7; ++dayOffset) {
        tm candidate = nowLocal;
        candidate.tm_mday += dayOffset;
        candidate.tm_hour = pump.hour;
        candidate.tm_min = pump.minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        const time_t timestamp = mktime(&candidate);
        if (timestamp == static_cast<time_t>(-1) || timestamp < nowTimestamp) continue;

        tm normalized{};
        localtime_r(&timestamp, &normalized);
        if (!scheduledOn(pump, normalized)) continue;
        if (static_cast<uint64_t>(timestamp) > std::numeric_limits<uint32_t>::max()) return result;

        result.valid = true;
        result.timestamp = static_cast<uint32_t>(timestamp);
        return result;
    }
    return result;
}

SchedulerManager::DateResult SchedulerManager::getEstimatedLastDose(size_t pumpIndex) const {
    DateResult result;
    if (pumps == nullptr || pumpIndex >= pumps->getPumpCount()) return result;

    const PumpConfig& pump = pumpAt(pumpIndex);
    uint32_t doses = calculateRemainingDoses(pump);
    if (!pump.enabled || pump.daysMask == 0 || doses == 0) return result;

    tm nowLocal{};
    time_t nowTimestamp = 0;
    if (!getReliableLocalTime(nowLocal, nowTimestamp)) return result;

    for (uint32_t dayOffset = 0; dayOffset <= MAX_ESTIMATE_DAYS; ++dayOffset) {
        tm candidate = nowLocal;
        candidate.tm_mday += static_cast<int>(dayOffset);
        candidate.tm_hour = pump.hour;
        candidate.tm_min = pump.minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        const time_t timestamp = mktime(&candidate);
        if (timestamp == static_cast<time_t>(-1) || timestamp < nowTimestamp) continue;

        tm normalized{};
        localtime_r(&timestamp, &normalized);
        if (!scheduledOn(pump, normalized)) continue;

        if (--doses == 0) {
            if (static_cast<uint64_t>(timestamp) > std::numeric_limits<uint32_t>::max()) return result;
            result.valid = true;
            result.timestamp = static_cast<uint32_t>(timestamp);
            return result;
        }
    }
    return result;
}

void SchedulerManager::printNextDoses() const {
    for (size_t i = 0; pumps != nullptr && i < pumps->getPumpCount(); ++i) {
        const DateResult next = getNextDose(i);
        if (!next.valid) continue;
        const time_t timestamp = static_cast<time_t>(next.timestamp);
        tm local{};
        localtime_r(&timestamp, &local);
        Serial.printf("[SCHEDULER] Nastepna dawka pompy %u: %04d-%02d-%02d %02d:%02d\n",
                      static_cast<unsigned>(i + 1), local.tm_year + 1900,
                      local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
    }
}

bool SchedulerManager::isAutomaticDosingActive() const {
    return automaticDosingActive;
}

const char* SchedulerManager::getSuspensionReason() const {
    return suspensionReason;
}

void SchedulerManager::setOtaInProgress(bool inProgress) { otaInProgress = inProgress; }

bool SchedulerManager::isOtaInProgress() const { return otaInProgress; }
