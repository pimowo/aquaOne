#include <Arduino.h>

#include <AquaCore/System/SystemService.h>
#include <AquaCore/System/DeviceIdentity.h>
#include <AquaCore/Logging/Logger.h>
#include <AquaCore/Logging/SerialLogSink.h>
#include <AquaCore/Web/CoreWebProjectionPublisher.h>
#include <AquaCore/Web/CoreWebProjectionSources.h>
#include <AquaCore/Web/Esp32ActionBridgeSynchronizer.h>
#include <AquaCore/Web/Esp32SnapshotSynchronizer.h>
#include <AquaCore/Web/EspIdfWebTransport.h>
#include <AquaCore/Web/NativeWebService.h>

#include "app_config.h"
#include "secrets.h"
#include "PumpManager.h"
#include "PumpDriver.h"
#include "MqttManager.h"
#include "DiagnosticsManager.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "DoserDiagnosticsProjection.h"
#include "DoserNativeOtaRoute.h"
#include "DoserNativeWeb.h"
#include "DoserNativeWebStartup.h"
#include "DoserOtaRuntime.h"
#include "DoserRestartRuntime.h"

AquaCore::SystemService systemService;
AquaCore::SerialLogSink serialLogSink(Serial);
AquaCore::Logger logger(serialLogSink);

TimeManager timeManager;
PumpManager pumpManager;
PumpDriver pumpDriver;
MqttManager mqttManager;
DiagnosticsManager diagnosticsManager;
SchedulerManager schedulerManager;
WiFiManager wifiManager;

// Reverse destruction stops HTTPD in nativeWebService before any borrowed
// route, bridge, snapshot, synchronizer, or transport context is destroyed.
AquaCore::Web::EspIdfWebTransport nativeTransport;
AquaCore::Web::Esp32SnapshotSynchronizer systemSnapshotSync;
AquaCore::Web::PublishedSnapshot<AquaCore::Web::CoreSystemProjection>
    systemSnapshot(systemSnapshotSync);
AquaCore::Web::Esp32SnapshotSynchronizer diagnosticsSnapshotSync;
AquaCore::Web::PublishedSnapshot<AquaCore::Web::CoreDiagnosticsProjection>
    diagnosticsSnapshot(diagnosticsSnapshotSync);
AquaCore::Web::SystemServiceWebProjectionSource systemProjectionSource(systemService);
DoserManagerDiagnosticsFacts diagnosticsFacts(
    systemService, timeManager, wifiManager, diagnosticsManager);
DoserDiagnosticsProjectionSource diagnosticsProjectionSource(diagnosticsFacts);
AquaCore::Web::CoreWebProjectionPublisher projectionPublisher(
    systemProjectionSource, &diagnosticsProjectionSource,
    systemSnapshot, diagnosticsSnapshot);

AquaCore::Web::Esp32ActionBridgeSynchronizer normalActionSync;
DoserWebBridge webBridge(normalActionSync);
DoserRestartRuntime restartRuntime(pumpDriver);
DoserWebApplication webApplication(webBridge, restartRuntime);
AquaCore::Web::Esp32ActionBridgeSynchronizer otaActionSync;
StreamingUploadBridge uploadBridge(otaActionSync);
AquaCore::Web::Esp32SnapshotSynchronizer capacitySnapshotSync;
DoserOtaCapacitySnapshot capacitySnapshot(capacitySnapshotSync);
DoserOtaRuntime otaRuntime(pumpDriver, schedulerManager);
DoserOtaApplication otaApplication(
    uploadBridge, otaRuntime, webApplication, capacitySnapshot);
DoserNativeWebRoutes nativeRoutes(webBridge, WEB_USER, WEB_PASS, &uploadBridge);
DoserNativeOtaRoute otaRoute(
    uploadBridge, capacitySnapshot, otaRuntime, WEB_USER, WEB_PASS);
AquaCore::Web::NativeWebService nativeWebService(
    nativeTransport, systemSnapshot, diagnosticsSnapshot);
unsigned long lastStatusPrint = 0;

void setup() {
    Serial.begin(115200);
    const AquaCore::DeviceIdentity identity(
        "dosing-controller",
        APP_NAME,
        APP_VERSION,
        "ESP32-S3-SuperMini"
    );
    systemService.begin(identity);

    logger.info("System", "AquaCore initialized");

    pumpDriver.begin();
    Serial.println();
    Serial.println("==================================");
    Serial.printf("%s v%s\n", APP_NAME, APP_VERSION);
    Serial.println("==================================");

    timeManager.begin();
    timeManager.printStatus();
    wifiManager.begin();

    if (!pumpManager.begin()) {
        Serial.println("[PUMPS] Blad inicjalizacji");
        return;
    }
    if (!pumpManager.isConfigurationInitialized()) {
        Serial.println("[PUMPS] Pierwsze uruchomienie - zapis konfiguracji testowej");
        constexpr uint8_t MONDAY_TO_FRIDAY = 0x1F;
        const uint32_t timestamp = timeManager.getUtcTimestamp();
        const bool configured =
            pumpManager.setName(0, "Mikro") &&
            pumpManager.setEnabled(0, true) &&
            pumpManager.setDose(0, 4.5F) &&
            pumpManager.setSchedule(0, 8, 15, MONDAY_TO_FRIDAY) &&
            pumpManager.setCalibration(0, 0.72F) &&
            pumpManager.setRemainingMl(0, 444.0F, timestamp);
        if (!configured || !pumpManager.saveAll())
            Serial.println("[PUMPS] Blad zapisu konfiguracji testowej");
    }
    pumpManager.printReport();

    schedulerManager.begin(pumpManager, timeManager, pumpDriver);
    schedulerManager.printNextDoses();
    diagnosticsManager.begin(timeManager, pumpManager, schedulerManager, mqttManager);
    mqttManager.begin(pumpManager, schedulerManager, timeManager, diagnosticsManager, pumpDriver);
    webApplication.setOtaSessionView(&uploadBridge);
    const bool webOk = startDoserNativeWeb(
        nativeWebService, nativeTransport, projectionPublisher,
        otaApplication, capacitySnapshot, nativeRoutes, otaRoute,
        otaRuntime.availableFirmwareSpace());
    Serial.println(webOk ? "[WEB] Native server started"
                         : "[WEB] Native server start failed");
}

void loop() {
    pumpDriver.loop();
    wifiManager.loop();
    timeManager.loop();
    otaApplication.processOne();
    webApplication.processOne();
    schedulerManager.loop();
    diagnosticsManager.loop();
    projectionPublisher.update();
    if (!otaApplication.blocksMqttService()) mqttManager.loop();
    otaApplication.serviceRestartWatchdog();
    webApplication.serviceRestart();

    if (millis() - lastStatusPrint >= 10000) {
        lastStatusPrint = millis();
        timeManager.printStatus();
    }
    yield();
}
