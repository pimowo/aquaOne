#include <Arduino.h>

#include "app_config.h"
#include "secrets.h"
#include "PumpManager.h"
#include "PumpDriver.h"
#include "MqttManager.h"
#include "DiagnosticsManager.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "WebManager.h"

TimeManager timeManager;
PumpManager pumpManager;
PumpDriver pumpDriver;
MqttManager mqttManager;
DiagnosticsManager diagnosticsManager;
SchedulerManager schedulerManager;
WiFiManager wifiManager;
WebManager webManager;
unsigned long lastStatusPrint = 0;

void setup() {
    Serial.begin(115200);
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
        const uint32_t timestamp = timeManager.getUtcTime().unixtime();
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
    webManager.begin(timeManager, mqttManager, diagnosticsManager, schedulerManager, wifiManager, pumpDriver);
}

void loop() {
    pumpDriver.loop();
    wifiManager.loop();
    timeManager.loop();
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
