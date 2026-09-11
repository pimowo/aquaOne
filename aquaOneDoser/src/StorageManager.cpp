#include "StorageManager.h"
#include "app_config.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr char NVS_NAMESPACE[] = "aquadoser";
constexpr char KEY_LEGACY_VERSION[] = "cfgVer";
constexpr char KEY_LEGACY_INITIALIZED[] = "cfgInit";

void legacyKeyFor(char* out, size_t size, size_t index, const char* field) {
    snprintf(out, size, "p%u_%s", static_cast<unsigned>(index), field);
}

bool validFloat(float value) {
    return std::isfinite(value) && value >= 0.0F;
}
} // namespace

StorageManager::StorageManager()
    : configStorage_(configBackend_, NVS_NAMESPACE, "cfg_a", "cfg_b"),
      runtimeStorage_(runtimeBackend_, NVS_NAMESPACE, "rt_a", "rt_b") {
}

StorageManager::~StorageManager() {
    end();
}

bool StorageManager::isValidDateKey(uint32_t dateKey, bool allowZero) {
    if (dateKey == 0) {
        return allowZero;
    }

    const uint32_t year = dateKey / 10000UL;
    const uint32_t month = (dateKey / 100UL) % 100UL;
    const uint32_t day = dateKey % 100UL;

    if (year < 2023UL || year > 2099UL) {
        return false;
    }
    if (month < 1UL || month > 12UL) {
        return false;
    }

    uint32_t maxDays = 31UL;
    if (month == 4UL || month == 6UL || month == 9UL || month == 11UL) {
        maxDays = 30UL;
    } else if (month == 2UL) {
        const bool leap = (year % 4UL == 0UL && year % 100UL != 0UL) || (year % 400UL == 0UL);
        maxDays = leap ? 29UL : 28UL;
    }

    return (day >= 1UL && day <= maxDays);
}

bool StorageManager::validateConfigPayload(const void* payload, size_t size) {
    if (payload == nullptr || size != sizeof(DoserConfigPayload)) {
        return false;
    }

    const auto* cfg = static_cast<const DoserConfigPayload*>(payload);
    if (cfg->magic != CONFIG_MAGIC) {
        return false;
    }

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        const auto& p = cfg->pumps[i];
        if (p.name[PUMP_NAME_LENGTH - 1] != '\0') {
            return false;
        }
        if (!std::isfinite(p.calibrationMlPerSec) || p.calibrationMlPerSec < 0.0F || p.calibrationMlPerSec > 1000.0F) {
            return false;
        }
        if (!std::isfinite(p.doseMl) || p.doseMl < 0.0F || p.doseMl > MAX_SINGLE_DOSE_ML) {
            return false;
        }
        if (p.hour > 23 || p.minute > 59 || (p.daysMask & 0x80) != 0) {
            return false;
        }
    }

    return true;
}

bool StorageManager::validateRuntimePayload(const void* payload, size_t size) {
    if (payload == nullptr || size != sizeof(DoserRuntimePayload)) {
        return false;
    }

    const auto* rt = static_cast<const DoserRuntimePayload*>(payload);
    if (rt->magic != RUNTIME_MAGIC) {
        return false;
    }

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        const auto& p = rt->pumps[i];
        if (!std::isfinite(p.remainingMl) || p.remainingMl < 0.0F || p.remainingMl > MAX_REMAINING_ML) {
            return false;
        }
        if (!isValidDateKey(p.lastDoseDate, true)) {
            return false;
        }
        if (!isValidDateKey(p.lastMissedDoseDate, true)) {
            return false;
        }
    }

    return true;
}

void StorageManager::setDefaults(PumpConfig& pump, size_t index) {
    pump = {};
    snprintf(pump.name, sizeof(pump.name), "Pompa %u", static_cast<unsigned>(index + 1));
    pump.hour = 8;
    pump.minute = 0;
    pump.daysMask = 0;
    pump.enabled = false;
    pump.calibrationMlPerSec = 0.0F;
    pump.doseMl = 0.0F;
    pump.remainingMl = 0.0F;
    pump.reservoirSetTimestamp = 0;
    pump.lastDoseDate = 0;
    pump.lastMissedDoseDate = 0;
}

void StorageManager::sanitize(PumpConfig& pump, size_t index) {
    pump.name[PUMP_NAME_LENGTH - 1] = '\0';
    if (!pump.name[0]) {
        snprintf(pump.name, sizeof(pump.name), "Pompa %u", static_cast<unsigned>(index + 1));
    }
    if (!validFloat(pump.calibrationMlPerSec) || pump.calibrationMlPerSec > 1000.0F) {
        pump.calibrationMlPerSec = 0.0F;
    }
    if (!validFloat(pump.doseMl) || pump.doseMl > MAX_SINGLE_DOSE_ML) {
        pump.doseMl = 0.0F;
    }
    if (pump.hour > 23) pump.hour = 8;
    if (pump.minute > 59) pump.minute = 0;
    pump.daysMask &= 0x7F;
    if (!validFloat(pump.remainingMl) || pump.remainingMl > MAX_REMAINING_ML) {
        pump.remainingMl = 0.0F;
    }
    if (!isValidDateKey(pump.lastDoseDate, true)) {
        pump.lastDoseDate = 0;
    }
    if (!isValidDateKey(pump.lastMissedDoseDate, true)) {
        pump.lastMissedDoseDate = 0;
    }
}

void StorageManager::setConfigDefaults(DoserConfigPayload& cfg) {
    cfg = {};
    cfg.magic = CONFIG_MAGIC;
    cfg.initialized = false;
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        snprintf(cfg.pumps[i].name, sizeof(cfg.pumps[i].name), "Pompa %u", static_cast<unsigned>(i + 1));
        cfg.pumps[i].hour = 8;
        cfg.pumps[i].minute = 0;
        cfg.pumps[i].daysMask = 0;
        cfg.pumps[i].enabled = false;
        cfg.pumps[i].calibrationMlPerSec = 0.0F;
        cfg.pumps[i].doseMl = 0.0F;
    }
}

void StorageManager::setRuntimeDefaults(DoserRuntimePayload& rt) {
    rt = {};
    rt.magic = RUNTIME_MAGIC;
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        rt.pumps[i].remainingMl = 0.0F;
        rt.pumps[i].reservoirSetTimestamp = 0;
        rt.pumps[i].lastDoseDate = 0;
        rt.pumps[i].lastMissedDoseDate = 0;
    }
}

void StorageManager::populatePayloadsFromPumps(
    const PumpConfig (&pumps)[PUMP_COUNT],
    DoserConfigPayload& cfg,
    DoserRuntimePayload& rt
) const {
    cfg = {};
    cfg.magic = CONFIG_MAGIC;
    cfg.initialized = configurationInitialized_;

    rt = {};
    rt.magic = RUNTIME_MAGIC;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        cfg.pumps[i].enabled = pumps[i].enabled;
        strncpy(cfg.pumps[i].name, pumps[i].name, PUMP_NAME_LENGTH - 1);
        cfg.pumps[i].name[PUMP_NAME_LENGTH - 1] = '\0';
        cfg.pumps[i].calibrationMlPerSec = pumps[i].calibrationMlPerSec;
        cfg.pumps[i].doseMl = pumps[i].doseMl;
        cfg.pumps[i].hour = pumps[i].hour;
        cfg.pumps[i].minute = pumps[i].minute;
        cfg.pumps[i].daysMask = pumps[i].daysMask;

        rt.pumps[i].remainingMl = pumps[i].remainingMl;
        rt.pumps[i].reservoirSetTimestamp = pumps[i].reservoirSetTimestamp;
        rt.pumps[i].lastDoseDate = pumps[i].lastDoseDate;
        rt.pumps[i].lastMissedDoseDate = pumps[i].lastMissedDoseDate;
    }
}

void StorageManager::populatePumpsFromPayloads(
    const DoserConfigPayload& cfg,
    const DoserRuntimePayload& rt,
    PumpConfig (&pumps)[PUMP_COUNT]
) {
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        pumps[i].enabled = cfg.pumps[i].enabled;
        strncpy(pumps[i].name, cfg.pumps[i].name, PUMP_NAME_LENGTH - 1);
        pumps[i].name[PUMP_NAME_LENGTH - 1] = '\0';
        pumps[i].calibrationMlPerSec = cfg.pumps[i].calibrationMlPerSec;
        pumps[i].doseMl = cfg.pumps[i].doseMl;
        pumps[i].hour = cfg.pumps[i].hour;
        pumps[i].minute = cfg.pumps[i].minute;
        pumps[i].daysMask = cfg.pumps[i].daysMask;

        pumps[i].remainingMl = rt.pumps[i].remainingMl;
        pumps[i].reservoirSetTimestamp = rt.pumps[i].reservoirSetTimestamp;
        pumps[i].lastDoseDate = rt.pumps[i].lastDoseDate;
        pumps[i].lastMissedDoseDate = rt.pumps[i].lastMissedDoseDate;

        sanitize(pumps[i], i);
    }
}

bool StorageManager::tryReadLegacyConfig(DoserConfigPayload& cfg) {
    Preferences legacy;
    if (!legacy.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    const uint16_t storedVersion = legacy.getUShort(KEY_LEGACY_VERSION, 0);
    const bool legacyInitialized = legacy.getBool(KEY_LEGACY_INITIALIZED, false);

    if (!legacyInitialized || (storedVersion != 1 && storedVersion != 2)) {
        legacy.end();
        return false;
    }

    cfg = {};
    cfg.magic = CONFIG_MAGIC;
    cfg.initialized = true;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        auto& p = cfg.pumps[i];
        snprintf(p.name, sizeof(p.name), "Pompa %u", static_cast<unsigned>(i + 1));
        p.hour = 8;
        p.minute = 0;
        p.daysMask = 0;
        p.enabled = false;
        p.calibrationMlPerSec = 0.0F;
        p.doseMl = 0.0F;

        char key[16];
        legacyKeyFor(key, sizeof(key), i, "valid");
        const bool validPump = legacy.getBool(key, false);

        if (validPump) {
            legacyKeyFor(key, sizeof(key), i, "en");
            p.enabled = legacy.getBool(key, false);

            legacyKeyFor(key, sizeof(key), i, "name");
            const size_t nameBytes = legacy.getBytesLength(key);
            if (nameBytes > 0 && nameBytes <= sizeof(p.name)) {
                legacy.getBytes(key, p.name, nameBytes);
                p.name[sizeof(p.name) - 1] = '\0';
            }

            legacyKeyFor(key, sizeof(key), i, "cal");
            p.calibrationMlPerSec = legacy.getFloat(key, 0.0F);

            legacyKeyFor(key, sizeof(key), i, "dose");
            p.doseMl = legacy.getFloat(key, 0.0F);

            legacyKeyFor(key, sizeof(key), i, "hour");
            p.hour = legacy.getUChar(key, 8);

            legacyKeyFor(key, sizeof(key), i, "min");
            p.minute = legacy.getUChar(key, 0);

            legacyKeyFor(key, sizeof(key), i, "days");
            p.daysMask = legacy.getUChar(key, 0);

            bool safetyFault = false;
            if (!validFloat(p.calibrationMlPerSec) || p.calibrationMlPerSec <= 0.0F || p.calibrationMlPerSec > 1000.0F) {
                p.calibrationMlPerSec = 0.0F;
                safetyFault = true;
            }
            if (!validFloat(p.doseMl) || p.doseMl <= 0.0F || p.doseMl > MAX_SINGLE_DOSE_ML) {
                p.doseMl = 0.0F;
                safetyFault = true;
            }
            if (p.hour > 23 || p.minute > 59 || p.daysMask == 0 || (p.daysMask & 0x80) != 0) {
                p.hour = 8;
                p.minute = 0;
                p.daysMask = 0;
                safetyFault = true;
            }
            if (p.name[sizeof(p.name) - 1] != '\0' || !p.name[0]) {
                snprintf(p.name, sizeof(p.name), "Pompa %u", static_cast<unsigned>(i + 1));
            }

            if (safetyFault) {
                p.enabled = false;
            }
        }
    }

    legacy.end();
    return true;
}

bool StorageManager::tryReadLegacyRuntime(DoserRuntimePayload& rt) {
    Preferences legacy;
    if (!legacy.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    const uint16_t storedVersion = legacy.getUShort(KEY_LEGACY_VERSION, 0);
    const bool legacyInitialized = legacy.getBool(KEY_LEGACY_INITIALIZED, false);

    if (!legacyInitialized || (storedVersion != 1 && storedVersion != 2)) {
        legacy.end();
        return false;
    }

    rt = {};
    rt.magic = RUNTIME_MAGIC;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        auto& p = rt.pumps[i];
        p.remainingMl = 0.0F;
        p.reservoirSetTimestamp = 0;
        p.lastDoseDate = 0;
        p.lastMissedDoseDate = 0;

        char key[16];
        legacyKeyFor(key, sizeof(key), i, "valid");
        const bool validPump = legacy.getBool(key, false);

        if (validPump) {
            legacyKeyFor(key, sizeof(key), i, "remain");
            p.remainingMl = legacy.getFloat(key, 0.0F);
            if (!validFloat(p.remainingMl) || p.remainingMl > MAX_REMAINING_ML) {
                p.remainingMl = 0.0F;
            }

            legacyKeyFor(key, sizeof(key), i, "rset");
            p.reservoirSetTimestamp = legacy.getUInt(key, 0);

            legacyKeyFor(key, sizeof(key), i, "last");
            const uint32_t lastDate = legacy.getUInt(key, 0);
            p.lastDoseDate = isValidDateKey(lastDate, false) ? lastDate : 0;

            legacyKeyFor(key, sizeof(key), i, "miss");
            const uint32_t missDate = legacy.getUInt(key, 0);
            p.lastMissedDoseDate = isValidDateKey(missDate, false) ? missDate : 0;
        }
    }

    legacy.end();
    return true;
}

bool StorageManager::begin() {
    if (opened_) {
        return true;
    }

    if (!configStorage_.begin(sizeof(DoserConfigPayload), SCHEMA_VERSION, validateConfigPayload) ||
        !runtimeStorage_.begin(sizeof(DoserRuntimePayload), SCHEMA_VERSION, validateRuntimePayload)) {
        Serial.println("[STORAGE] Blad inicjalizacji Core StorageService");
        return false;
    }

    opened_ = true;

    DoserConfigPayload cfgPayload{};
    DoserRuntimePayload rtPayload{};

    const bool hasCoreConfig = configStorage_.load(&cfgPayload);
    const bool hasCoreRuntime = runtimeStorage_.load(&rtPayload);

    if (hasCoreConfig && hasCoreRuntime) {
        configurationInitialized_ = cfgPayload.initialized;
        populatePumpsFromPayloads(cfgPayload, rtPayload, persisted_);
        for (size_t i = 0; i < PUMP_COUNT; ++i) {
            persistedValid_[i] = true;
        }
        Serial.println("[STORAGE] Wczytano konfiguracje i stan z Core StorageService");
        return true;
    }

    bool configNeedsSave = false;
    bool runtimeNeedsSave = false;

    if (hasCoreConfig) {
        Serial.println("[STORAGE] Rekord Config poprawny w Core Storage");
    } else {
        if (tryReadLegacyConfig(cfgPayload)) {
            Serial.println("[STORAGE] Zaimportowano rekord Config ze starego NVS");
        } else {
            Serial.println("[STORAGE] Brak rekordu Config - uzyto wartosci domyslnych");
            setConfigDefaults(cfgPayload);
        }
        configNeedsSave = true;
    }

    if (hasCoreRuntime) {
        Serial.println("[STORAGE] Rekord Runtime poprawny w Core Storage");
    } else {
        if (tryReadLegacyRuntime(rtPayload)) {
            Serial.println("[STORAGE] Zaimportowano rekord Runtime ze starego NVS");
        } else {
            Serial.println("[STORAGE] Brak rekordu Runtime - uzyto wartosci domyslnych");
            setRuntimeDefaults(rtPayload);
        }
        runtimeNeedsSave = true;
    }

    // Fail-Safe Duplicate Dosing Protection:
    // Jeśli rekord Runtime musiał zostać odzyskany z legacy lub zresetowany do domyślnych (brak pewnego Core Runtime),
    // a pompa w konfiguracji jest włączona, wymagamy poprawnej historii lastDoseDate.
    // Jeśli historia dawki nie jest znana (lastDoseDate == 0), wyłączamy pompę i zapisujemy bezpieczny stan w Config.
    if (!hasCoreRuntime) {
        for (size_t i = 0; i < PUMP_COUNT; ++i) {
            if (cfgPayload.pumps[i].enabled && rtPayload.pumps[i].lastDoseDate == 0) {
                cfgPayload.pumps[i].enabled = false;
                configNeedsSave = true;
                Serial.printf("[STORAGE] Pompa %u wylaczona po odzyskiwaniu Runtime (brak historii lastDoseDate)\n",
                              static_cast<unsigned>(i + 1));
            }
        }
    }

    if (configNeedsSave) {
        if (!configStorage_.save(&cfgPayload)) {
            Serial.println("[STORAGE] Blad zapisu odzyskanego rekordu Config do Core");
        }
    }

    if (runtimeNeedsSave) {
        if (!runtimeStorage_.save(&rtPayload)) {
            Serial.println("[STORAGE] Blad zapisu odzyskanego rekordu Runtime do Core");
        }
    }

    configurationInitialized_ = cfgPayload.initialized;
    populatePumpsFromPayloads(cfgPayload, rtPayload, persisted_);
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        persistedValid_[i] = true;
    }

    return true;
}

void StorageManager::end() {
    opened_ = false;
}

bool StorageManager::loadAll(PumpConfig (&pumps)[PUMP_COUNT]) {
    if (!opened_) {
        return false;
    }

    memcpy(pumps, persisted_, sizeof(pumps));
    Serial.printf("[STORAGE] Wczytano %u pomp\n", static_cast<unsigned>(PUMP_COUNT));
    return true;
}

bool StorageManager::savePump(size_t index, const PumpConfig& source) {
    if (!opened_ || index >= PUMP_COUNT) {
        return false;
    }

    PumpConfig pump = source;
    sanitize(pump, index);

    const PumpConfig& old = persisted_[index];
    const bool fresh = !persistedValid_[index];

    const bool configChanged = fresh ||
        pump.enabled != old.enabled ||
        strncmp(pump.name, old.name, sizeof(pump.name)) != 0 ||
        pump.calibrationMlPerSec != old.calibrationMlPerSec ||
        pump.doseMl != old.doseMl ||
        pump.hour != old.hour ||
        pump.minute != old.minute ||
        pump.daysMask != old.daysMask;

    const bool runtimeChanged = fresh ||
        pump.remainingMl != old.remainingMl ||
        pump.reservoirSetTimestamp != old.reservoirSetTimestamp ||
        pump.lastDoseDate != old.lastDoseDate ||
        pump.lastMissedDoseDate != old.lastMissedDoseDate;

    lastOperationChanged_ = configChanged || runtimeChanged;

    persisted_[index] = pump;
    persistedValid_[index] = true;

    bool ok = true;

    if (configChanged) {
        DoserConfigPayload cfg{};
        DoserRuntimePayload rtDummy{};
        populatePayloadsFromPumps(persisted_, cfg, rtDummy);
        ok &= configStorage_.save(&cfg);
    }

    if (runtimeChanged) {
        DoserConfigPayload cfgDummy{};
        DoserRuntimePayload rt{};
        populatePayloadsFromPumps(persisted_, cfgDummy, rt);
        ok &= runtimeStorage_.save(&rt);
    }

    if (ok) {
        if (lastOperationChanged_) {
            Serial.printf("[STORAGE] Zapisano pompe %u\n", static_cast<unsigned>(index + 1));
        }
    } else {
        Serial.printf("[STORAGE] Blad zapisu pompy %u\n", static_cast<unsigned>(index + 1));
    }

    return ok;
}

bool StorageManager::saveAll(const PumpConfig (&pumps)[PUMP_COUNT]) {
    if (!opened_) {
        return false;
    }

    configurationInitialized_ = true;

    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        persisted_[i] = pumps[i];
        sanitize(persisted_[i], i);
        persistedValid_[i] = true;
    }

    DoserConfigPayload cfg{};
    DoserRuntimePayload rt{};
    populatePayloadsFromPumps(persisted_, cfg, rt);

    const bool ok = configStorage_.save(&cfg) && runtimeStorage_.save(&rt);
    lastOperationChanged_ = ok;

    if (ok) {
        Serial.println("[STORAGE] Zapis konfiguracji i stanu zakonczony pomyslnie");
    } else {
        Serial.println("[STORAGE] Blad zapisu konfiguracji i stanu");
    }

    return ok;
}

bool StorageManager::isConfigurationInitialized() const {
    return configurationInitialized_;
}

bool StorageManager::didLastOperationChangeData() const {
    return lastOperationChanged_;
}
