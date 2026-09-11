#include "StorageManager.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr char NVS_NAMESPACE[] = "aquadoser";
constexpr char KEY_VERSION[] = "cfgVer";
constexpr char KEY_INITIALIZED[] = "cfgInit";
void keyFor(char* out, size_t size, size_t index, const char* field) {
    snprintf(out, size, "p%u_%s", static_cast<unsigned>(index), field);
}
bool validFloat(float value) { return std::isfinite(value) && value >= 0.0F; }
}

bool StorageManager::begin() {
    if (opened) return true;
    opened = preferences.begin(NVS_NAMESPACE, false);
    if (!opened) {
        Serial.println("[STORAGE] Nie mozna otworzyc NVS");
        return false;
    }

    storedVersion = preferences.getUShort(KEY_VERSION, 0);
    const bool supportedVersion = storedVersion == 1 || storedVersion == CONFIG_VERSION;
    configurationInitialized = supportedVersion && preferences.getBool(KEY_INITIALIZED, false);

    if (storedVersion == 0) {
        Serial.println("[STORAGE] Brak konfiguracji - wartosci domyslne");
    } else if (!supportedVersion) {
        Serial.printf("[STORAGE] Schemat v%u nieobslugiwany - wartosci domyslne\n", storedVersion);
    } else if (storedVersion == 1 && configurationInitialized) {
        if (preferences.putUShort(KEY_VERSION, CONFIG_VERSION) != sizeof(uint16_t)) {
            Serial.println("[STORAGE] Blad migracji schematu v1 -> v2");
            return false;
        }
        storedVersion = CONFIG_VERSION;
        Serial.println("[STORAGE] Migracja schematu v1 -> v2 zakonczona");
    } else {
        Serial.printf("[STORAGE] Schemat konfiguracji v%u\n", storedVersion);
    }
    return true;
}
void StorageManager::end() {
    if (opened) { preferences.end(); opened = false; }
}
void StorageManager::setDefaults(PumpConfig& pump, size_t index) {
    pump = {};
    snprintf(pump.name, sizeof(pump.name), "Pompa %u", static_cast<unsigned>(index + 1));
    pump.hour = 8;
}
void StorageManager::sanitize(PumpConfig& pump, size_t index) {
    pump.name[PUMP_NAME_LENGTH - 1] = '\0';
    if (!pump.name[0]) snprintf(pump.name, sizeof(pump.name), "Pompa %u", static_cast<unsigned>(index + 1));
    if (!validFloat(pump.calibrationMlPerSec)) pump.calibrationMlPerSec = 0;
    if (!validFloat(pump.doseMl)) pump.doseMl = 0;
    if (pump.hour > 23) pump.hour = 8;
    if (pump.minute > 59) pump.minute = 0;
    pump.daysMask &= 0x7F;
    if (!validFloat(pump.remainingMl)) pump.remainingMl = 0;
}
bool StorageManager::loadPump(size_t index, PumpConfig& pump) {
    setDefaults(pump, index);
    char key[16];
    keyFor(key, sizeof(key), index, "valid");
    if (!configurationInitialized || !preferences.getBool(key, false)) {
        persisted[index] = pump;
        persistedValid[index] = false;
        return true;
    }
    keyFor(key, sizeof(key), index, "en"); pump.enabled = preferences.getBool(key, pump.enabled);
    keyFor(key, sizeof(key), index, "name");
    const size_t nameBytes = preferences.getBytesLength(key);
    if (nameBytes > 0 && nameBytes <= sizeof(pump.name)) {
        preferences.getBytes(key, pump.name, nameBytes);
        pump.name[sizeof(pump.name) - 1] = '\0';
    }
    keyFor(key, sizeof(key), index, "cal"); pump.calibrationMlPerSec = preferences.getFloat(key, 0);
    keyFor(key, sizeof(key), index, "dose"); pump.doseMl = preferences.getFloat(key, 0);
    keyFor(key, sizeof(key), index, "hour"); pump.hour = preferences.getUChar(key, 8);
    keyFor(key, sizeof(key), index, "min"); pump.minute = preferences.getUChar(key, 0);
    keyFor(key, sizeof(key), index, "days"); pump.daysMask = preferences.getUChar(key, 0);
    keyFor(key, sizeof(key), index, "remain"); pump.remainingMl = preferences.getFloat(key, 0);
    keyFor(key, sizeof(key), index, "rset"); pump.reservoirSetTimestamp = preferences.getUInt(key, 0);
    keyFor(key, sizeof(key), index, "last"); pump.lastDoseDate = preferences.getUInt(key, 0);
    keyFor(key, sizeof(key), index, "miss"); pump.lastMissedDoseDate = preferences.getUInt(key, 0);
    sanitize(pump, index);
    persisted[index] = pump;
    persistedValid[index] = true;
    return true;
}
bool StorageManager::loadAll(PumpConfig (&pumps)[PUMP_COUNT]) {
    if (!opened) return false;
    for (size_t i = 0; i < PUMP_COUNT; ++i) if (!loadPump(i, pumps[i])) return false;
    Serial.printf("[STORAGE] Wczytano %u pomp\n", static_cast<unsigned>(PUMP_COUNT));
    return true;
}
bool StorageManager::writeSchemaMetadata() {
    bool ok = true;
    if (preferences.getUShort(KEY_VERSION, 0) != CONFIG_VERSION)
        ok &= preferences.putUShort(KEY_VERSION, CONFIG_VERSION) == sizeof(uint16_t);
    if (!preferences.getBool(KEY_INITIALIZED, false))
        ok &= preferences.putBool(KEY_INITIALIZED, true) == sizeof(bool);
    if (ok) configurationInitialized = true;
    return ok;
}
bool StorageManager::savePump(size_t index, const PumpConfig& source) {
    if (!opened || index >= PUMP_COUNT) return false;
    PumpConfig pump = source;
    sanitize(pump, index);
    const PumpConfig& old = persisted[index];
    const bool fresh = !persistedValid[index];
    const bool changed = fresh || pump.enabled != old.enabled ||
        strncmp(pump.name, old.name, sizeof(pump.name)) != 0 ||
        pump.calibrationMlPerSec != old.calibrationMlPerSec || pump.doseMl != old.doseMl ||
        pump.hour != old.hour || pump.minute != old.minute || pump.daysMask != old.daysMask ||
        pump.remainingMl != old.remainingMl ||
        pump.reservoirSetTimestamp != old.reservoirSetTimestamp ||
        pump.lastDoseDate != old.lastDoseDate ||
        pump.lastMissedDoseDate != old.lastMissedDoseDate;
    lastOperationChanged = changed;
    bool ok = true;
    char key[16];
#define SAVE(field, suffix, method, bytes) do { if (fresh || pump.field != old.field) { \
    keyFor(key, sizeof(key), index, suffix); ok &= preferences.method(key, pump.field) == (bytes); } } while (false)
    SAVE(enabled, "en", putBool, sizeof(bool));
    if (fresh || strncmp(pump.name, old.name, sizeof(pump.name))) {
        keyFor(key, sizeof(key), index, "name");
        const size_t bytes = strnlen(pump.name, sizeof(pump.name) - 1) + 1;
        ok &= preferences.putBytes(key, pump.name, bytes) == bytes;
    }
    SAVE(calibrationMlPerSec, "cal", putFloat, sizeof(float));
    SAVE(doseMl, "dose", putFloat, sizeof(float));
    SAVE(hour, "hour", putUChar, sizeof(uint8_t));
    SAVE(minute, "min", putUChar, sizeof(uint8_t));
    SAVE(daysMask, "days", putUChar, sizeof(uint8_t));
    SAVE(remainingMl, "remain", putFloat, sizeof(float));
    SAVE(reservoirSetTimestamp, "rset", putUInt, sizeof(uint32_t));
    SAVE(lastDoseDate, "last", putUInt, sizeof(uint32_t));
    SAVE(lastMissedDoseDate, "miss", putUInt, sizeof(uint32_t));
#undef SAVE
    if (fresh) {
        keyFor(key, sizeof(key), index, "valid");
        ok &= preferences.putBool(key, true) == sizeof(bool);
    }
    if (ok) {
        persisted[index] = pump;
        persistedValid[index] = true;
        if (changed) Serial.printf("[STORAGE] Zapisano pompe %u\n", static_cast<unsigned>(index + 1));
    } else Serial.printf("[STORAGE] Blad zapisu pompy %u\n", static_cast<unsigned>(index + 1));
    return ok;
}
bool StorageManager::saveAll(const PumpConfig (&pumps)[PUMP_COUNT]) {
    if (!opened) return false;
    bool ok = true;
    bool anyChanged = false;
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        ok &= savePump(i, pumps[i]);
        anyChanged |= lastOperationChanged;
    }
    ok &= writeSchemaMetadata();
    lastOperationChanged = anyChanged;
    if (ok && anyChanged) Serial.println("[STORAGE] Zapis konfiguracji zakonczony");
    return ok;
}bool StorageManager::isConfigurationInitialized() const { return configurationInitialized; }
bool StorageManager::didLastOperationChangeData() const { return lastOperationChanged; }
