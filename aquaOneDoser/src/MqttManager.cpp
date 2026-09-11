#include "MqttManager.h"

#include "PumpManager.h"
#include "PumpDriver.h"
#include "DiagnosticsManager.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "app_config.h"
#include "secrets.h"

#include <WiFi.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
#define BASE_TOPIC "pmw/aquadoser"
constexpr char STATUS_TOPIC[] = "pmw/aquadoser/status";
constexpr char CLIENT_ID[] = "pmw-aquadoser";
constexpr char ONLINE[] = "online";
constexpr char OFFLINE[] = "offline";
const char* DAY_KEYS[] =
    {"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};
}

MqttManager* MqttManager::instance = nullptr;

bool MqttManager::begin(PumpManager& pumpManager, SchedulerManager& schedulerManager,
                        TimeManager& timeManager, DiagnosticsManager& diagnosticsManager,
                        PumpDriver& pumpDriver) {
    diagnostics = &diagnosticsManager;
    pumps = &pumpManager;
    driver = &pumpDriver;
    scheduler = &schedulerManager;
    time = &timeManager;
    instance = this;
    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setCallback(callback);
    client.setBufferSize(MQTT_BUFFER_SIZE);
    Serial.printf("[MQTT] Buffer size: %u\n", MQTT_BUFFER_SIZE);
    client.setKeepAlive(30);
    client.setSocketTimeout(1);
    lastConnectAttempt = millis() - RECONNECT_INTERVAL_MS;
    return true;
}

void MqttManager::loop() {
    if (WiFi.status() != WL_CONNECTED || !client.connected()) {
        if (wasConnected) {
            Serial.println("[MQTT] Disconnected");
            wasConnected = false;
        }
        startupState = StartupState::IDLE;
        discovery.cancel();
        pendingStatePump = -1;
        if (WiFi.status() == WL_CONNECTED) connectIfNeeded();
        return;
    }

    client.loop();

    if (startupState == StartupState::WAIT_AFTER_CONNECT) {
        if (millis() - mqttConnectedAt >= MQTT_DISCOVERY_START_DELAY_MS) {
            discovery.beginPublishing();
            startupState = StartupState::PUBLISHING;
        }
        return;
    }

    if (startupState == StartupState::PUBLISHING) {
        discovery.loop(client, *pumps);
        if (discovery.isComplete()) {
            startupState = StartupState::PUBLISH_STATES;
            snapshotDiagnosticsPublished = false;
            pendingStatePump = -1;
            Serial.println("[HA] Publishing full state snapshot");
        }
        return;
    }

    if (startupState == StartupState::PUBLISH_STATES) {
        if (!snapshotDiagnosticsPublished) {
            publishDiagnostics();
            snapshotDiagnosticsPublished = true;
            pendingStatePump = 0;
            return;
        }
        if (pendingStatePump >= 0 && pendingStatePump < static_cast<int8_t>(PUMP_COUNT)) {
            publishPumpState(static_cast<size_t>(pendingStatePump++));
            return;
        }
        pendingStatePump = -1;
        publishedPumpRevision = pumps->getRevision();
        publishedDiagnosticsRevision = diagnostics->getRevision();
        startupState = StartupState::COMPLETE;
        Serial.println("[HA] State snapshot complete");
        return;
    }

    if (startupState != StartupState::COMPLETE) return;

    if (pendingStatePump >= 0) {
        publishPumpState(static_cast<size_t>(pendingStatePump));
        ++pendingStatePump;
        if (pendingStatePump >= static_cast<int8_t>(PUMP_COUNT)) {
            pendingStatePump = -1;
            publishedPumpRevision = pumps->getRevision();
        }
    } else if (publishedPumpRevision != pumps->getRevision()) {
        publishAllPumpStates();
    }

    if (publishedDiagnosticsRevision != diagnostics->getRevision()) {
        publishDiagnostics();
        publishAllPumpStates();
        publishedDiagnosticsRevision = diagnostics->getRevision();
    } else if (millis() - lastDiagnosticsPublish >= DIAGNOSTICS_INTERVAL_MS) {
        publishDiagnostics();
    }
}
bool MqttManager::isConnected() { return client.connected(); }

void MqttManager::connectIfNeeded() {
    if (MQTT_HOST[0] == '\0') {
        if (!disabledLogged) {
            Serial.println("[MQTT] MQTT_HOST nieustawiony - MQTT wylaczony");
            disabledLogged = true;
        }
        return;
    }

    const unsigned long now = millis();
    if (now - lastConnectAttempt < RECONNECT_INTERVAL_MS) return;
    lastConnectAttempt = now;
    Serial.println("[MQTT] Connecting...");

    const bool connected = client.connect(CLIENT_ID, MQTT_USER, MQTT_PASS,
                                          STATUS_TOPIC, 1, true, OFFLINE);
    if (!connected) {
        Serial.printf("[MQTT] Connection failed, state=%d - kolejna proba za 10 s\n",
                      client.state());
        return;
    }
    onConnected();
}

void MqttManager::onConnected() {
    wasConnected = true;
    disabledLogged = false;
    Serial.println("[MQTT] Connected");
    client.publish(STATUS_TOPIC, ONLINE, true);
    client.publish(BASE_TOPIC "/device/online/state", "ON", true);
    client.subscribe(BASE_TOPIC "/pump/+/+/set");
    client.subscribe(BASE_TOPIC "/pump/+/day/+/set");
    client.subscribe(BASE_TOPIC "/device/command/restart");
    mqttConnectedAt = millis();
    startupState = StartupState::WAIT_AFTER_CONNECT;
    snapshotDiagnosticsPublished = false;
    pendingStatePump = -1;
    Serial.println("[HA] Discovery scheduled");
}
void MqttManager::callback(char* topic, byte* payload, unsigned int length) {
    if (instance != nullptr) instance->handleMessage(topic, payload, length);
}

void MqttManager::handleMessage(const char* topic, const byte* payload, unsigned int length) {
    if (strcmp(topic, BASE_TOPIC "/device/command/restart") == 0) {
        if (length == 5 && memcmp(payload, "PRESS", 5) == 0) {
            Serial.println("[MQTT] Restart requested");
            if (driver != nullptr) driver->stopAll();
            ESP.restart();
        } else {
            Serial.println("[MQTT] Invalid payload rejected");
        }
        return;
    }

    char value[96];
    if (length == 0 || length >= sizeof(value)) {
        Serial.println("[MQTT] Invalid payload rejected");
        return;
    }
    memcpy(value, payload, length);
    value[length] = '\0';

    unsigned pumpNumber = 0;
    char path[64];
    if (sscanf(topic, BASE_TOPIC "/pump/%u/%63s", &pumpNumber, path) != 2 ||
        pumpNumber < 1 || pumpNumber > PUMP_COUNT) {
        Serial.println("[MQTT] Invalid topic rejected");
        return;
    }

    const size_t index = pumpNumber - 1;
    if (!applyPumpCommand(index, path, value)) {
        rejectCommand(index, path);
        return;
    }

    if (startupState == StartupState::COMPLETE) {
        publishPumpState(index);
        publishedPumpRevision = pumps->getRevision();
    }
}

void MqttManager::rejectCommand(size_t pumpIndex, const char* path) {
    if (strcmp(path, "time/set") != 0)
        Serial.println("[MQTT] Invalid payload rejected");
    if (pumpIndex < PUMP_COUNT && startupState == StartupState::COMPLETE)
        publishPumpState(pumpIndex);
}

bool MqttManager::applyPumpCommand(size_t index, const char* path, const char* value) {
    PumpConfig& pump = pumps->getPump(index);
    bool accepted = false;

    if (strcmp(path, "enabled/set") == 0) {
        if (strcmp(value, "ON") == 0) accepted = pumps->setEnabled(index, true);
        else if (strcmp(value, "OFF") == 0) accepted = pumps->setEnabled(index, false);
    } else if (strcmp(path, "name/set") == 0) {
        accepted = pumps->setName(index, value);
    } else if (strcmp(path, "dose/set") == 0) {
        float number;
        accepted = parseFloat(value, number) && number >= 0.1F && number <= MAX_SINGLE_DOSE_ML &&
                   pumps->setDose(index, number);
        if (accepted)
            Serial.printf("[MQTT] Pump %u dose changed: %.2f ml\n", index + 1, number);
    } else if (strcmp(path, "calibration/set") == 0) {
        float number;
        accepted = parseFloat(value, number) && number >= 0.0F && number <= 1000.0F &&
                   pumps->setCalibration(index, number);
    } else if (strcmp(path, "remaining/set") == 0) {
        float number;
        const uint32_t timestamp = time->getUtcTimestamp();
        accepted = parseFloat(value, number) && number >= 0.0F && number <= MAX_REMAINING_ML &&
                   timestamp >= 1700000000UL &&
                   pumps->setRemainingMl(index, number, timestamp);
        if (accepted)
            Serial.printf("[MQTT] Pump %u remaining changed: %.2f ml\n", index + 1, number);
    } else if (strcmp(path, "time/set") == 0) {
        uint8_t hour = 0;
        uint8_t minute = 0;
        if (!parseTime(value, hour, minute)) {
            Serial.printf("[MQTT] Invalid time payload rejected: %s\n", value);
            return false;
        }
        accepted = pumps->setSchedule(index, hour, minute, pump.daysMask);
        if (accepted)
            Serial.printf("[MQTT] Pump %u time changed: %02u:%02u\n",
                          index + 1, hour, minute);
    } else if (strncmp(path, "day/", 4) == 0) {
        const char* dayStart = path + 4;
        for (uint8_t day = 0; day < 7; ++day) {
            char expected[32];
            snprintf(expected, sizeof(expected), "%s/set", DAY_KEYS[day]);
            if (strcmp(dayStart, expected) != 0) continue;

            uint8_t mask = pump.daysMask;
            if (strcmp(value, "ON") == 0)
                mask |= static_cast<uint8_t>(1U << day);
            else if (strcmp(value, "OFF") == 0)
                mask &= static_cast<uint8_t>(~(1U << day));
            else
                return false;

            accepted = pumps->setSchedule(index, pump.hour, pump.minute, mask);
            break;
        }
    } else if (strcmp(path, "manual_dose/set") == 0) {
        return strcmp(value, "PRESS") == 0 && startManualDose(index);
    }

    return accepted && pumps->savePump(index);
}

bool MqttManager::startManualDose(size_t index) {
    if (driver == nullptr || scheduler == nullptr || manualDoseActive ||
        scheduler->isOtaInProgress() || driver->anyRunning()) return false;

    PumpConfig& pump = pumps->getPump(index);
    if (!(pump.calibrationMlPerSec > 0.0F) || !(pump.doseMl > 0.0F) ||
        !std::isfinite(pump.calibrationMlPerSec) || !std::isfinite(pump.doseMl) ||
        !std::isfinite(pump.remainingMl) || pump.remainingMl < pump.doseMl ||
        pump.doseMl > MAX_SINGLE_DOSE_ML)
        return false;

    const float runtimeSec = pump.doseMl / pump.calibrationMlPerSec;
    if (!std::isfinite(runtimeSec) || runtimeSec > MAX_PUMP_RUNTIME_SEC) return false;
    const unsigned long runtimeMs = static_cast<unsigned long>(ceil(runtimeSec * 1000.0F));
    if (runtimeMs == 0 || !driver->startPump(static_cast<uint8_t>(index))) return false;

    manualDoseActive = true;
    manualPump = static_cast<int8_t>(index);
    manualStartedAt = millis();
    manualRuntimeMs = runtimeMs;
    Serial.printf("[MQTT] Manual dose started - pump %u, %.2f ml, %lu ms\n",
                  static_cast<unsigned>(index + 1), pump.doseMl, runtimeMs);
    return true;
}

void MqttManager::serviceManualDose() {
    if (!manualDoseActive || driver == nullptr) return;
    if (manualPump < 0 || manualPump >= static_cast<int8_t>(PUMP_COUNT)) {
        driver->stopAll();
        manualDoseActive = false;
        manualPump = -1;
        Serial.println("[SAFETY] Invalid manual dose state");
        return;
    }

    const size_t index = static_cast<size_t>(manualPump);
    if (!driver->isRunning(static_cast<uint8_t>(index))) {
        Serial.printf("[MQTT] Manual dose interrupted - pump %u, no state saved\n",
                      static_cast<unsigned>(index + 1));
        manualDoseActive = false;
        manualPump = -1;
        return;
    }
    if (millis() - manualStartedAt < manualRuntimeMs) return;

    if (!driver->stopPump(static_cast<uint8_t>(index))) {
        driver->stopAll();
        Serial.printf("[MQTT] Manual dose stop error - pump %u, no state saved\n",
                      static_cast<unsigned>(index + 1));
        manualDoseActive = false;
        manualPump = -1;
        return;
    }

    PumpConfig& pump = pumps->getPump(index);
    pump.remainingMl -= pump.doseMl;
    if (pump.remainingMl < 0.0F) pump.remainingMl = 0.0F;
    if (!pumps->savePump(index))
        Serial.println("[MQTT] Manual dose completed, but NVS save failed");
    else
        Serial.printf("[MQTT] Manual dose completed - pump %u, remaining %.2f ml\n",
                      static_cast<unsigned>(index + 1), pump.remainingMl);
    manualDoseActive = false;
    manualPump = -1;
}
void MqttManager::publishText(const char* topic, const char* value) {
    client.publish(topic, value, true);
}

void MqttManager::publishFloat(const char* topic, float value, uint8_t decimals) {
    char payload[32];
    snprintf(payload, sizeof(payload), "%.*f", decimals, value);
    publishText(topic, payload);
}

void MqttManager::publishUInt(const char* topic, uint32_t value) {
    char payload[24];
    snprintf(payload, sizeof(payload), "%lu", static_cast<unsigned long>(value));
    publishText(topic, payload);
}

void MqttManager::publishDiagnostics() {
    if (!client.connected()) return;

    char topic[128];
    char value[64];
    publishText(BASE_TOPIC "/device/online/state", "ON");

    snprintf(topic, sizeof(topic), BASE_TOPIC "/device/rssi/state");
    snprintf(value, sizeof(value), "%d", WiFi.RSSI());
    publishText(topic, value);
    publishText(BASE_TOPIC "/device/ip/state", WiFi.localIP().toString().c_str());
    publishUInt(BASE_TOPIC "/device/uptime/state", millis() / 1000UL);
    publishText(BASE_TOPIC "/device/firmware/state", APP_VERSION);
    publishText(BASE_TOPIC "/device/rtc/state", time->isRtcOk() ? "ON" : "OFF");
    publishText(BASE_TOPIC "/device/ntp/state", time->isNtpSynced() ? "ON" : "OFF");
    publishText(BASE_TOPIC "/device/system_status/state", diagnostics->getSystemStatusText());
    publishText(BASE_TOPIC "/device/automatic_dosing/state",
                diagnostics->isAutomaticDosingActive() ? "ON" : "OFF");
    publishText(BASE_TOPIC "/device/suspension_reason/state", diagnostics->getSuspensionReason());

    const uint32_t now = time->getUtcTimestamp();
    formatTimestamp(now, value, sizeof(value));
    publishText(BASE_TOPIC "/device/local_time/state", value);
    formatTimestamp(time->getLastNtpSyncTimestamp(), value, sizeof(value));
    publishText(BASE_TOPIC "/device/last_ntp/state", value);
    lastDiagnosticsPublish = millis();
}

void MqttManager::publishAllPumpStates() {
    pendingStatePump = 0;
}

void MqttManager::publishPumpState(size_t index) {
    if (!client.connected() || index >= PUMP_COUNT) return;

    const PumpConfig& pump = pumps->getPump(index);
    char topic[160];
    char value[64];
    const unsigned number = static_cast<unsigned>(index + 1);

#define PUMP_TOPIC(field) \
    snprintf(topic, sizeof(topic), BASE_TOPIC "/pump/%u/" field "/state", number)

    PUMP_TOPIC("enabled");
    publishText(topic, pump.enabled ? "ON" : "OFF");
    PUMP_TOPIC("name");
    publishText(topic, pump.name);
    PUMP_TOPIC("dose");
    publishFloat(topic, pump.doseMl, 2);
    PUMP_TOPIC("time");
    snprintf(value, sizeof(value), "%02u:%02u:00", pump.hour, pump.minute);
    publishText(topic, value);

    for (uint8_t day = 0; day < 7; ++day) {
        snprintf(topic, sizeof(topic), BASE_TOPIC "/pump/%u/day/%s/state",
                 number, DAY_KEYS[day]);
        publishText(topic, (pump.daysMask & (1U << day)) ? "ON" : "OFF");
    }

    PUMP_TOPIC("calibration");
    publishFloat(topic, pump.calibrationMlPerSec, 3);
    PUMP_TOPIC("remaining");
    publishFloat(topic, pump.remainingMl, 2);

    PUMP_TOPIC("reservoir_set");
    formatTimestamp(pump.reservoirSetTimestamp, value, sizeof(value));
    publishText(topic, value);

    PUMP_TOPIC("remaining_doses");
    publishUInt(topic, SchedulerManager::calculateRemainingDoses(pump));

    PUMP_TOPIC("last_dose");
    if (pump.lastDoseDate == 0) {
        strcpy(value, "unknown");
    } else {
        snprintf(value, sizeof(value), "%04lu-%02lu-%02lu",
                 static_cast<unsigned long>(pump.lastDoseDate / 10000UL),
                 static_cast<unsigned long>((pump.lastDoseDate / 100UL) % 100UL),
                 static_cast<unsigned long>(pump.lastDoseDate % 100UL));
    }
    publishText(topic, value);

    PUMP_TOPIC("next_dose");
    const SchedulerManager::DateResult next = scheduler->getNextDose(index);
    formatTimestamp(next.valid ? next.timestamp : 0, value, sizeof(value));
    publishText(topic, value);

    PUMP_TOPIC("estimated_empty");
    const SchedulerManager::DateResult empty = scheduler->getEstimatedLastDose(index);
    formatTimestamp(empty.valid ? empty.timestamp : 0, value, sizeof(value));
    publishText(topic, value);

    PUMP_TOPIC("status");
    publishText(topic, diagnostics->getPumpStatusText(index));
    PUMP_TOPIC("no_liquid");
    publishText(topic, diagnostics->hasNoLiquid(index) ? "ON" : "OFF");
    PUMP_TOPIC("low_liquid");
    publishText(topic, diagnostics->hasLowLiquid(index) ? "ON" : "OFF");
    PUMP_TOPIC("missed_dose");
    publishText(topic, diagnostics->hasMissedDoseToday(index) ? "ON" : "OFF");
    PUMP_TOPIC("last_missed_dose");
    if (pump.lastMissedDoseDate == 0) strcpy(value, "unknown");
    else snprintf(value, sizeof(value), "%04lu-%02lu-%02lu",
                  static_cast<unsigned long>(pump.lastMissedDoseDate / 10000UL),
                  static_cast<unsigned long>((pump.lastMissedDoseDate / 100UL) % 100UL),
                  static_cast<unsigned long>(pump.lastMissedDoseDate % 100UL));
    publishText(topic, value);

#undef PUMP_TOPIC
}

bool MqttManager::parseFloat(const char* value, float& result) {
    char* end = nullptr;
    result = strtof(value, &end);
    return end != value && *end == '\0' && std::isfinite(result);
}

bool MqttManager::parseTime(const char* value, uint8_t& hour, uint8_t& minute) {
    if (value == nullptr) return false;
    const size_t length = strlen(value);
    if ((length != 5 && length != 8) || value[2] != ':' ||
        (length == 8 && value[5] != ':')) return false;

    for (size_t i = 0; i < length; ++i) {
        if (i == 2 || i == 5) continue;
        if (value[i] < '0' || value[i] > '9') return false;
    }

    hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
    minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
    const uint8_t second = length == 8
        ? static_cast<uint8_t>((value[6] - '0') * 10 + (value[7] - '0'))
        : 0;
    return hour <= 23 && minute <= 59 && second <= 59;
}

void MqttManager::formatTimestamp(uint32_t timestamp, char* output, size_t outputSize) {
    if (timestamp < 1700000000UL) {
        snprintf(output, outputSize, "unknown");
        return;
    }

    const time_t raw = static_cast<time_t>(timestamp);
    tm utc{};
    gmtime_r(&raw, &utc);
    strftime(output, outputSize, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
