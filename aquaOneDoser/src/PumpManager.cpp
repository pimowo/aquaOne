#include "PumpManager.h"
#include "app_config.h"
#include <cmath>
#include <cstring>

bool PumpManager::begin() {
    if (!storage.begin() || !storage.loadAll(pumps)) return false;
    Serial.printf("[PUMPS] Loaded %u pumps\n", static_cast<unsigned>(PUMP_COUNT));
    return true;
}
size_t PumpManager::getPumpCount() const { return PUMP_COUNT; }
bool PumpManager::validIndex(size_t index) { return index < PUMP_COUNT; }
bool PumpManager::validNonNegative(float value) { return std::isfinite(value) && value >= 0.0F; }
const PumpConfig& PumpManager::getPump(size_t index) const {
    static const PumpConfig invalidPump{};
    return validIndex(index) ? pumps[index] : invalidPump;
}
PumpConfig& PumpManager::getPump(size_t index) {
    static PumpConfig invalidPump{};
    return validIndex(index) ? pumps[index] : invalidPump;
}
bool PumpManager::setEnabled(size_t index, bool enabled) {
    if (!validIndex(index)) return false;
    pumps[index].enabled = enabled;
    return true;
}
bool PumpManager::setName(size_t index, const char* name) {
    if (!validIndex(index) || name == nullptr || name[0] == '\0') return false;
    const size_t length = strnlen(name, PUMP_NAME_LENGTH);
    if (length >= PUMP_NAME_LENGTH) return false;
    memcpy(pumps[index].name, name, length + 1);
    return true;
}
bool PumpManager::setCalibration(size_t index, float value) {
    if (!validIndex(index) || !validNonNegative(value)) return false;
    pumps[index].calibrationMlPerSec = value;
    return true;
}
bool PumpManager::setDose(size_t index, float value) {
    if (!validIndex(index) || !validNonNegative(value) || value > MAX_SINGLE_DOSE_ML) return false;
    pumps[index].doseMl = value;
    return true;
}
bool PumpManager::setSchedule(size_t index, uint8_t hour, uint8_t minute, uint8_t daysMask) {
    if (!validIndex(index) || hour > 23 || minute > 59 || (daysMask & 0x80)) return false;
    pumps[index].hour = hour;
    pumps[index].minute = minute;
    pumps[index].daysMask = daysMask;
    return true;
}
bool PumpManager::setRemainingMl(size_t index, float value, uint32_t timestamp) {
    if (!validIndex(index) || !validNonNegative(value) || value > MAX_REMAINING_ML) return false;
    pumps[index].remainingMl = value;
    pumps[index].reservoirSetTimestamp = timestamp;
    return true;
}
bool PumpManager::savePump(size_t index) {
    if (!validIndex(index) || !storage.savePump(index, pumps[index])) return false;
    if (storage.didLastOperationChangeData()) ++revision;
    return true;
}
bool PumpManager::saveAll() {
    if (!storage.saveAll(pumps)) return false;
    if (storage.didLastOperationChangeData()) ++revision;
    return true;
}
bool PumpManager::isConfigurationInitialized() const { return storage.isConfigurationInitialized(); }
uint32_t PumpManager::getRevision() const { return revision; }
void PumpManager::printReport() const {
    for (size_t i = 0; i < PUMP_COUNT; ++i) {
        const PumpConfig& p = pumps[i];
        Serial.printf("\n[PUMP %u]\n", static_cast<unsigned>(i + 1));
        Serial.printf("Name: %s\nEnabled: %s\n", p.name, p.enabled ? "YES" : "NO");
        Serial.printf("Calibration: %.3f ml/s\nDose: %.2f ml\n", p.calibrationMlPerSec, p.doseMl);
        Serial.printf("Schedule: %02u:%02u\nDays mask: 0x%02X\n", p.hour, p.minute, p.daysMask);
        Serial.printf("Remaining: %.2f ml\n", p.remainingMl);
        Serial.printf("Reservoir set timestamp: %lu\nLast dose date: %lu\n",
                      static_cast<unsigned long>(p.reservoirSetTimestamp),
                      static_cast<unsigned long>(p.lastDoseDate));
    }
}
