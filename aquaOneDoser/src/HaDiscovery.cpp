#include "HaDiscovery.h"

#include "app_config.h"
#include "PumpManager.h"

#include <cstdio>
#include <cstring>

namespace {
const char* mqttStateName(int state) {
    switch (state) {
        case MQTT_CONNECTION_TIMEOUT: return "MQTT_CONNECTION_TIMEOUT";
        case MQTT_CONNECTION_LOST: return "MQTT_CONNECTION_LOST";
        case MQTT_CONNECT_FAILED: return "MQTT_CONNECT_FAILED";
        case MQTT_DISCONNECTED: return "MQTT_DISCONNECTED";
        case MQTT_CONNECTED: return "MQTT_CONNECTED";
        case MQTT_CONNECT_BAD_PROTOCOL: return "MQTT_CONNECT_BAD_PROTOCOL";
        case MQTT_CONNECT_BAD_CLIENT_ID: return "MQTT_CONNECT_BAD_CLIENT_ID";
        case MQTT_CONNECT_UNAVAILABLE: return "MQTT_CONNECT_UNAVAILABLE";
        case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT_CONNECT_BAD_CREDENTIALS";
        case MQTT_CONNECT_UNAUTHORIZED: return "MQTT_CONNECT_UNAUTHORIZED";
        default: return "MQTT_UNKNOWN_STATE";
    }
}
#define BASE_TOPIC "pmw/aquadoser"
constexpr char AVAILABILITY_TOPIC[] = "pmw/aquadoser/status";
constexpr char DEVICE_JSON[] =
    "\"device\":{\"identifiers\":[\"pmw_aquadoser\"],\"name\":\"PMW AquaDoser\","
    "\"manufacturer\":\"PMW\",\"model\":\"AquaDoser 8\",\"sw_version\":\"" APP_VERSION "\"}";

}

bool HaDiscovery::sendConfig(PubSubClient& client, const char* component, const char* uniqueId,
                             const char* name, const char* stateTopic, const char* commandTopic,
                             const char* extra) {
    char topic[160];
    char payload[1300];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config", component, uniqueId);
    const bool hasState = stateTopic != nullptr && stateTopic[0] != '\0';
    const bool hasCommand = commandTopic != nullptr && commandTopic[0] != '\0';
    const int length = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s\"%s%s%s%s%s%s,"
        "\"availability_topic\":\"%s\",\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",%s%s}",
        name, uniqueId,
        hasState ? ",\"state_topic\":\"" : "", hasState ? stateTopic : "", hasState ? "\"" : "",
        hasCommand ? ",\"command_topic\":\"" : "", hasCommand ? commandTopic : "", hasCommand ? "\"" : "",
        AVAILABILITY_TOPIC, DEVICE_JSON, extra);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(payload)) {
        Serial.printf("[HA] Discovery payload too large: %s (%d bytes)\n", uniqueId, length);
        return false;
    }
    const size_t payloadSize = static_cast<size_t>(length);
    if (payloadSize > maxPayloadSize) maxPayloadSize = payloadSize;
    if (!client.publish(topic, payload, true)) {
        const int mqttState = client.state();
        Serial.printf("[HA] Discovery publish failed: %s\n", uniqueId);
        Serial.println("[MQTT] Publish failed");
        Serial.printf("[MQTT] connected: %s\n", client.connected() ? "YES" : "NO");
        Serial.printf("[MQTT] state: %d (%s)\n", mqttState, mqttStateName(mqttState));
        Serial.printf("[MQTT] free heap: %u\n", ESP.getFreeHeap());
        Serial.printf("[MQTT] topic length: %u\n", static_cast<unsigned>(strlen(topic)));
        Serial.printf("[MQTT] payload length: %u\n", static_cast<unsigned>(payloadSize));
        return false;
    }    return true;
}

void HaDiscovery::beginPublishing() {
    nextEntity = 0;
    retryNotBefore = 0;
    retryCount = 0;
    maxPayloadSize = 0;
    discoveryStartedAt = millis();
    Serial.println("[HA] Discovery started");
}

void HaDiscovery::cancel() {
    nextEntity = TOTAL_ENTITY_COUNT;
    retryNotBefore = 0;
    retryCount = 0;
}

void HaDiscovery::loop(PubSubClient& client, const PumpManager& pumps) {
    if (!client.connected() || !isPublishing()) return;
    const unsigned long now = millis();
    if (static_cast<long>(now - retryNotBefore) < 0) return;

    if (!publishEntity(client, pumps, nextEntity)) {
        unsigned long backoff = MQTT_DISCOVERY_RETRY_MS;
        for (uint8_t i = 0; i < retryCount && backoff < MQTT_DISCOVERY_RETRY_MAX_MS; ++i)
            backoff *= 2UL;
        if (backoff > MQTT_DISCOVERY_RETRY_MAX_MS) backoff = MQTT_DISCOVERY_RETRY_MAX_MS;
        if (retryCount < 3) ++retryCount;
        retryNotBefore = millis() + backoff;
        Serial.printf("[HA] Discovery retry in %lu ms (attempt %u)\n", backoff, retryCount);
        return;
    }

    retryCount = 0;
    ++nextEntity;
    retryNotBefore = millis() + MQTT_DISCOVERY_PUBLISH_INTERVAL_MS;
    if (nextEntity < TOTAL_ENTITY_COUNT && nextEntity % 25 == 0)
        Serial.printf("[HA] Discovery %u/%u\n", nextEntity, TOTAL_ENTITY_COUNT);

    if (nextEntity == TOTAL_ENTITY_COUNT) {
        Serial.printf("[HA] Discovery complete: %u/%u in %lu ms\n",
                      nextEntity, TOTAL_ENTITY_COUNT, millis() - discoveryStartedAt);
        Serial.printf("[HA] Max discovery payload: %u bytes\n",
                      static_cast<unsigned>(maxPayloadSize));
    }
}
bool HaDiscovery::isPublishing() const { return nextEntity < TOTAL_ENTITY_COUNT; }
bool HaDiscovery::isComplete() const { return nextEntity == TOTAL_ENTITY_COUNT; }
uint16_t HaDiscovery::getPublishedCount() const { return nextEntity; }
uint16_t HaDiscovery::getTotalCount() const { return TOTAL_ENTITY_COUNT; }
size_t HaDiscovery::getMaxPayloadSize() const { return maxPayloadSize; }
bool HaDiscovery::publishEntity(PubSubClient& client, const PumpManager& pumps, uint16_t index) {
    if (index < GLOBAL_ENTITY_COUNT) return publishGlobal(client, index);
    const uint16_t relative = index - GLOBAL_ENTITY_COUNT;
    return publishPump(client, pumps, relative / PUMP_ENTITY_COUNT, relative % PUMP_ENTITY_COUNT);
}

bool HaDiscovery::publishGlobal(PubSubClient& client, uint16_t index) {
    struct Entity { const char* component; const char* key; const char* name; const char* extra; };
    static const Entity entities[GLOBAL_ENTITY_COUNT] = {
        {"binary_sensor", "online", "Online", ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"connectivity\""},
        {"sensor", "rssi", "Wi-Fi RSSI", ",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\",\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\""},
        {"sensor", "ip", "IP", ",\"entity_category\":\"diagnostic\",\"enabled_by_default\":false"},
        {"sensor", "uptime", "Uptime", ",\"unit_of_measurement\":\"s\",\"device_class\":\"duration\",\"state_class\":\"total_increasing\",\"entity_category\":\"diagnostic\",\"enabled_by_default\":false"},
        {"sensor", "firmware", "Firmware", ",\"entity_category\":\"diagnostic\",\"enabled_by_default\":false"},
        {"binary_sensor", "rtc", "RTC status", ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"connectivity\",\"entity_category\":\"diagnostic\""},
        {"binary_sensor", "ntp", "NTP status", ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"connectivity\",\"entity_category\":\"diagnostic\""},
        {"sensor", "local_time", "Aktualny czas lokalny", ",\"device_class\":\"timestamp\""},
        {"sensor", "last_ntp", "Ostatnia synchronizacja NTP", ",\"device_class\":\"timestamp\",\"entity_category\":\"diagnostic\",\"enabled_by_default\":false"},
        {"button", "restart", "Restart ESP", ",\"payload_press\":\"PRESS\",\"device_class\":\"restart\",\"entity_category\":\"config\""},
        {"sensor", "system_status", "Status urzadzenia", ""},
        {"binary_sensor", "automatic_dosing", "Automatyczne dozowanie", ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\""},
        {"sensor", "suspension_reason", "Powod wstrzymania", ""}
    };

    const Entity& entity = entities[index];
    char uniqueId[80], stateTopic[120], commandTopic[120];
    snprintf(uniqueId, sizeof(uniqueId), "pmw_aquadoser_%s", entity.key);
    snprintf(stateTopic, sizeof(stateTopic), BASE_TOPIC "/device/%s/state", entity.key);
    snprintf(commandTopic, sizeof(commandTopic), BASE_TOPIC "/device/command/restart");
    return sendConfig(client, entity.component, uniqueId, entity.name,
                      index == 9 ? nullptr : stateTopic,
                      index == 9 ? commandTopic : nullptr, entity.extra);
}

bool HaDiscovery::publishPump(PubSubClient& client, const PumpManager& pumps,
                              size_t pumpIndex, uint16_t entityIndex) {
    static const char* dayKeys[] =
        {"monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};
    static const char* dayNames[] =
        {"Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"};

    const unsigned pumpNumber = static_cast<unsigned>(pumpIndex + 1);
    const char* component = "sensor";
    const char* key = "";
    const char* label = "";
    const char* extra = "";
    char dynamicExtra[256];
    bool commandEnabled = false;
    bool dayEntity = false;

    switch (entityIndex) {
        case 0: component = "switch"; key = "enabled"; label = "Włączona"; commandEnabled = true;
                extra = ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\""; break;
        case 1: component = "text"; key = "name"; label = "Nazwa"; commandEnabled = true;
                extra = ",\"min\":1,\"max\":31,\"mode\":\"text\""; break;
        case 2: component = "number"; key = "dose"; label = "Dawka"; commandEnabled = true;
                snprintf(dynamicExtra, sizeof(dynamicExtra),
                         ",\"min\":0.1,\"max\":%.1f,\"step\":0.1,\"unit_of_measurement\":\"ml\",\"mode\":\"box\"",
                         MAX_SINGLE_DOSE_ML);
                extra = dynamicExtra; break;
        case 3: component = "time"; key = "time"; label = "Czas dozowania"; commandEnabled = true;
                break;
        case 11: component = "number"; key = "calibration"; label = "Kalibracja"; commandEnabled = true;
                 extra = ",\"min\":0,\"max\":1000,\"step\":0.001,\"unit_of_measurement\":\"ml/s\",\"mode\":\"box\""; break;
        case 12: component = "number"; key = "remaining"; label = "Stan płynu"; commandEnabled = true;
                 snprintf(dynamicExtra, sizeof(dynamicExtra),
                          ",\"min\":0,\"max\":%.1f,\"step\":0.1,\"unit_of_measurement\":\"ml\",\"mode\":\"box\"",
                          MAX_REMAINING_ML);
                 extra = dynamicExtra; break;
        case 13: key = "reservoir_set"; label = "Data ustawienia stanu";
                 extra = ",\"device_class\":\"timestamp\""; break;
        case 14: key = "remaining_doses"; label = "Pozostało dawek"; break;
        case 15: key = "last_dose"; label = "Ostatnia dawka"; break;
        case 16: key = "next_dose"; label = "Następna dawka";
                 extra = ",\"device_class\":\"timestamp\""; break;
        case 17: key = "estimated_empty"; label = "Przewidywane wyczerpanie";
                 extra = ",\"device_class\":\"timestamp\""; break;
        case 18: component = "button"; key = "manual_dose"; label = "Dozuj teraz"; commandEnabled = true;
                 extra = ",\"payload_press\":\"PRESS\""; break;
        case 19: key = "status"; label = "Status"; break;
        case 20: component = "binary_sensor"; key = "no_liquid"; label = "Brak plynu";
                 extra = ",\"device_class\":\"problem\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\""; break;
        case 21: component = "binary_sensor"; key = "low_liquid"; label = "Niski poziom plynu";
                 extra = ",\"device_class\":\"problem\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\""; break;
        case 22: component = "binary_sensor"; key = "missed_dose"; label = "Pominieta dawka";
                 extra = ",\"device_class\":\"problem\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\""; break;
        case 23: key = "last_missed_dose"; label = "Ostatnia pominieta dawka"; break;
        default:
            if (entityIndex >= 4 && entityIndex <= 10) {
                const size_t day = entityIndex - 4;
                component = "switch";
                key = dayKeys[day];
                label = dayNames[day];
                commandEnabled = true;
                dayEntity = true;
                extra = ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"";
            } else {
                return false;
            }
    }

    char uniqueId[96], name[96], stateTopic[160], commandTopic[160];
    snprintf(uniqueId, sizeof(uniqueId), "pmw_aquadoser_p%u_%s", pumpNumber, key);
    snprintf(name, sizeof(name), "Pompa %u %s", pumpNumber, label);

    if (dayEntity) {
        snprintf(stateTopic, sizeof(stateTopic), BASE_TOPIC "/pump/%u/day/%s/state", pumpNumber, key);
        snprintf(commandTopic, sizeof(commandTopic), BASE_TOPIC "/pump/%u/day/%s/set", pumpNumber, key);
    } else {
        snprintf(stateTopic, sizeof(stateTopic), BASE_TOPIC "/pump/%u/%s/state", pumpNumber, key);
        snprintf(commandTopic, sizeof(commandTopic), BASE_TOPIC "/pump/%u/%s/set", pumpNumber, key);
    }

    return sendConfig(client, component, uniqueId, name,
                      entityIndex == 18 ? nullptr : stateTopic,
                      commandEnabled ? commandTopic : nullptr, extra);
}
