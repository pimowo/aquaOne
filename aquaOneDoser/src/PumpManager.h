#pragma once
#include "PumpConfig.h"
#include "StorageManager.h"

class PumpManager {
public:
    bool begin();
    size_t getPumpCount() const;
    const PumpConfig& getPump(size_t index) const;
    PumpConfig& getPump(size_t index);
    bool setEnabled(size_t index, bool enabled);
    bool setName(size_t index, const char* name);
    bool setCalibration(size_t index, float mlPerSec);
    bool setDose(size_t index, float ml);
    bool setSchedule(size_t index, uint8_t hour, uint8_t minute, uint8_t daysMask);
    bool setRemainingMl(size_t index, float ml, uint32_t timestamp);
    bool savePump(size_t index);
    bool saveAll();
    bool isConfigurationInitialized() const;
    uint32_t getRevision() const;
    void printReport() const;
private:
    PumpConfig pumps[PUMP_COUNT]{};
    StorageManager storage;
    uint32_t revision = 0;
    static bool validIndex(size_t index);
    static bool validNonNegative(float value);
};
