#pragma once

#include <Preferences.h>
#include <AquaCore/Config/PreferencesStorageBackend.h>
#include <AquaCore/Config/StorageService.h>
#include "PumpConfig.h"

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool begin();
    void end();
    bool loadAll(PumpConfig (&pumps)[PUMP_COUNT]);
    bool savePump(size_t index, const PumpConfig& pump);
    bool saveAll(const PumpConfig (&pumps)[PUMP_COUNT]);
    bool isConfigurationInitialized() const;
    bool didLastOperationChangeData() const;

private:
    struct DoserPumpConfigRecord {
        bool enabled;
        char name[PUMP_NAME_LENGTH];
        float calibrationMlPerSec;
        float doseMl;
        uint8_t hour;
        uint8_t minute;
        uint8_t daysMask;
        uint8_t reserved[1];
    };

    struct DoserConfigPayload {
        uint32_t magic;
        bool initialized;
        uint8_t reserved[3];
        DoserPumpConfigRecord pumps[PUMP_COUNT];
    };

    struct DoserPumpRuntimeRecord {
        float remainingMl;
        uint32_t reservoirSetTimestamp;
        uint32_t lastDoseDate;
        uint32_t lastMissedDoseDate;
    };

    struct DoserRuntimePayload {
        uint32_t magic;
        DoserPumpRuntimeRecord pumps[PUMP_COUNT];
    };

    static constexpr uint32_t CONFIG_MAGIC = 0x444F5343UL;  // "DOSC"
    static constexpr uint32_t RUNTIME_MAGIC = 0x444F5352UL; // "DOSR"
    static constexpr uint16_t SCHEMA_VERSION = 1U;

    AquaCore::Config::PreferencesStorageBackend<Preferences> configBackend_;
    AquaCore::Config::StorageService configStorage_;

    AquaCore::Config::PreferencesStorageBackend<Preferences> runtimeBackend_;
    AquaCore::Config::StorageService runtimeStorage_;

    PumpConfig persisted_[PUMP_COUNT]{};
    bool persistedValid_[PUMP_COUNT]{};
    bool opened_ = false;
    bool configurationInitialized_ = false;
    bool lastOperationChanged_ = false;

    static void setDefaults(PumpConfig& pump, size_t index);
    static void sanitize(PumpConfig& pump, size_t index);
    static bool isValidDateKey(uint32_t dateKey, bool allowZero = true);
    static bool validateConfigPayload(const void* payload, size_t size);
    static bool validateRuntimePayload(const void* payload, size_t size);

    static void setConfigDefaults(DoserConfigPayload& cfg);
    static void setRuntimeDefaults(DoserRuntimePayload& rt);
    static bool tryReadLegacyConfig(DoserConfigPayload& cfg);
    static bool tryReadLegacyRuntime(DoserRuntimePayload& rt);

    void populatePayloadsFromPumps(const PumpConfig (&pumps)[PUMP_COUNT],
                                  DoserConfigPayload& cfg,
                                  DoserRuntimePayload& rt) const;
    void populatePumpsFromPayloads(const DoserConfigPayload& cfg,
                                  const DoserRuntimePayload& rt,
                                  PumpConfig (&pumps)[PUMP_COUNT]);
};
