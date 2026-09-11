#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "HaDiscovery.h"

class DiagnosticsManager;
class PumpManager;
class PumpDriver;
class SchedulerManager;
class TimeManager;

class MqttManager {
public:
    bool begin(PumpManager& pumpManager, SchedulerManager& schedulerManager,
               TimeManager& timeManager, DiagnosticsManager& diagnosticsManager,
               PumpDriver& pumpDriver);
    void loop();
    bool isConnected();

private:
    static constexpr unsigned long RECONNECT_INTERVAL_MS = 10000UL;
    static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 60000UL;

    enum class StartupState : uint8_t {
        IDLE, WAIT_AFTER_CONNECT, PUBLISHING, PUBLISH_STATES, COMPLETE
    };

    WiFiClient networkClient;
    PubSubClient client{networkClient};
    HaDiscovery discovery;
    DiagnosticsManager* diagnostics = nullptr;
    PumpManager* pumps = nullptr;
    PumpDriver* driver = nullptr;
    SchedulerManager* scheduler = nullptr;
    TimeManager* time = nullptr;
    unsigned long lastConnectAttempt = 0;
    unsigned long lastDiagnosticsPublish = 0;
    uint32_t publishedPumpRevision = 0;
    uint32_t publishedDiagnosticsRevision = 0;
    int8_t pendingStatePump = -1;
    StartupState startupState = StartupState::IDLE;
    unsigned long mqttConnectedAt = 0;
    bool snapshotDiagnosticsPublished = false;
    bool wasConnected = false;
    bool disabledLogged = false;
    bool manualDoseActive = false;
    int8_t manualPump = -1;
    unsigned long manualStartedAt = 0;
    unsigned long manualRuntimeMs = 0;

    static MqttManager* instance;
    static void callback(char* topic, byte* payload, unsigned int length);

    void connectIfNeeded();
    void onConnected();
    void handleMessage(const char* topic, const byte* payload, unsigned int length);
    void rejectCommand(size_t pumpIndex, const char* path);
    bool applyPumpCommand(size_t pumpIndex, const char* path, const char* value);
    bool startManualDose(size_t pumpIndex);
    void serviceManualDose();
    void publishDiagnostics();
    void publishAllPumpStates();
    void publishPumpState(size_t index);
    void publishText(const char* topic, const char* value);
    void publishFloat(const char* topic, float value, uint8_t decimals);
    void publishUInt(const char* topic, uint32_t value);
    static bool parseFloat(const char* value, float& result);
    static bool parseTime(const char* value, uint8_t& hour, uint8_t& minute);
    static void formatTimestamp(uint32_t timestamp, char* output, size_t outputSize);
};
