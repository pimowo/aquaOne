#pragma once

#include <Arduino.h>
#include <WebServer.h>

class DiagnosticsManager;
class MqttManager;
class PumpDriver;
class SchedulerManager;
class TimeManager;
class WiFiManager;

class WebManager {
public:
    bool begin(TimeManager& timeManager, MqttManager& mqttManager,
               DiagnosticsManager& diagnosticsManager, SchedulerManager& schedulerManager,
               WiFiManager& wifiManager, PumpDriver& pumpDriver);
    void loop();
    bool isOtaInProgress() const;

private:
    static constexpr uint16_t HTTP_PORT = 80;
    static constexpr unsigned long RESTART_DELAY_MS = 1000UL;

    WebServer server{HTTP_PORT};
    TimeManager* time = nullptr;
    MqttManager* mqtt = nullptr;
    DiagnosticsManager* diagnostics = nullptr;
    SchedulerManager* scheduler = nullptr;
    WiFiManager* wifi = nullptr;
    PumpDriver* driver = nullptr;
    bool routesConfigured = false;
    bool serverStarted = false;
    bool wifiWasConnected = false;
    bool otaInProgress = false;
    bool otaAuthorized = false;
    bool otaAccepted = false;
    bool otaSuccessful = false;
    String otaError;
    size_t expectedFirmwareSize = 0;
    uint8_t nextProgressPercent = 25;
    bool restartPending = false;
    unsigned long restartAt = 0;

    void configureRoutes();
    void startServer();
    bool authenticateAdmin();
    void handleRoot();
    void handleStatus();
    void handleRestart();
    void handleUpdatePage();
    void handleUpdateResult();
    void handleUpload();
    void failOta(const String& message);
    void scheduleRestart();
    void serviceDuringUpload();
    static String jsonEscape(const String& value);
    static String formatLocalTimestamp(uint32_t timestamp);
};