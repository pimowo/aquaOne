#pragma once
#include <Preferences.h>
#include "PumpConfig.h"

class StorageManager {
public:
    bool begin();
    void end();
    bool loadAll(PumpConfig (&pumps)[PUMP_COUNT]);
    bool savePump(size_t index, const PumpConfig& pump);
    bool saveAll(const PumpConfig (&pumps)[PUMP_COUNT]);
    bool isConfigurationInitialized() const;
    bool didLastOperationChangeData() const;
private:
    Preferences preferences;
    PumpConfig persisted[PUMP_COUNT]{};
    bool persistedValid[PUMP_COUNT]{};
    bool opened = false;
    bool configurationInitialized = false;
    bool lastOperationChanged = false;
    uint16_t storedVersion = 0;
    static void setDefaults(PumpConfig& pump, size_t index);
    static void sanitize(PumpConfig& pump, size_t index);
    bool loadPump(size_t index, PumpConfig& pump);
    bool writeSchemaMetadata();
};
