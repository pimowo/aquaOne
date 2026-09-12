#include <Arduino.h>

#include <AquaCore/System/SystemService.h>
#include <AquaCore/System/DeviceIdentity.h>
#include <AquaCore/Logging/Logger.h>
#include <AquaCore/Logging/SerialLogSink.h>
#include <AquaCore/Web/Esp32WebBackend.h>
#include <AquaCore/Web/WebConfig.h>
#include <AquaCore/Web/WebService.h>

#include "app_config.h"
#include "secrets.h"
#include "PumpManager.h"
#include "PumpDriver.h"
#include "MqttManager.h"
#include "DiagnosticsManager.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "DoserWebRuntime.h"
#include "WebManager.h"

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
WebManager webManager;
DoserWebRuntime webRuntime(
    timeManager, mqttManager, diagnosticsManager,
    schedulerManager, wifiManager, pumpDriver
);
AquaCore::Web::Esp32WebBackend webBackend;
AquaCore::Web::WebService webService(webBackend, systemService);
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
    const bool legacyRoutesOk = webManager.begin(webService, webRuntime, WEB_USER, WEB_PASS);
    AquaCore::Web::WebConfig webConfig {};
    webConfig.enabled = true;
    webConfig.port = 80U;
    webConfig.navigationMask = 0U;
    const bool webOk = legacyRoutesOk && webService.begin(webConfig);
    Serial.println(webOk ? "[WEB] Server started" : "[WEB] Server start failed");
}

void loop() {
    pumpDriver.loop();
    wifiManager.loop();
    timeManager.loop();
    webService.update();
    webManager.loop();
    schedulerManager.loop();
    diagnosticsManager.loop();
    mqttManager.loop();

    if (millis() - lastStatusPrint >= 10000) {
        lastStatusPrint = millis();
        timeManager.printStatus();
    }
    yield();
}
