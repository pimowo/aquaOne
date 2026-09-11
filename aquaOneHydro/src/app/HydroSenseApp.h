#pragma once

#include <AquaCore/System/SystemService.h>
#include <AquaCore/System/DeviceIdentity.h>

#include <AquaCore/Network/Esp32NetworkBackend.h>
#include <AquaCore/Network/NetworkService.h>

#include <AquaCore/Web/Esp32WebBackend.h>
#include <AquaCore/Web/WebService.h>

#include "hardware/Pump.h"
#include "hardware/FloatSensor.h"
#include "hardware/UltrasonicSensor.h"
#include "hardware/Button.h"
#include "hardware/Buzzer.h"

#include "hydrosense/HydroSenseConfig.h"
#include "hydrosense/HydroSenseConfigStorage.h"
#include "hydrosense/WaterTank.h"
#include "hydrosense/WaterReserve.h"
#include "hydrosense/TopupController.h"
#include "hydrosense/AlarmManager.h"
#include "hydrosense/BuzzerController.h"
#include "hydrosense/SystemStatus.h"

#include "web/HydroSenseDashboard.h"
#include "web/HydroSenseApi.h"

#include "web/HydroSenseSettingsPage.h"
#include "web/HydroSenseSettingsApi.h"

#include "web/HydroSenseControlPage.h"
#include "web/HydroSenseControlApi.h"

#include "web/HydroSenseDiagnosticsPage.h"

class HydroSenseApp
{
public:
    HydroSenseApp();

    void begin();
    void update();

    void setServiceMode(bool enabled);
    bool isServiceMode() const;

    const SystemStatus& status() const;

private:
    void loadConfiguration();

    void beginSystem();
    void beginNetwork();
    void beginWeb();

    void handleButton();
    void handleRestartRequest();

    void updateStatus();

    HydroSenseConfig config_;

    HydroSenseConfigStorage
        configStorage_;


    // =========================================================
    // SPRZĘT
    // =========================================================

    Pump pump_;
    FloatSensor floatSensor_;
    UltrasonicSensor ultrasonicSensor_;

    Button button_;
    Buzzer buzzer_;


    // =========================================================
    // LOGIKA
    // =========================================================

    WaterTank waterTank_;
    WaterReserve waterReserve_;

    TopupController topupController_;
    AlarmManager alarmManager_;
    BuzzerController buzzerController_;

    SystemStatus status_;


    // =========================================================
    // SYSTEM
    // =========================================================

    AquaCore::SystemService
        systemService_;


    // =========================================================
    // NETWORK
    // =========================================================

    AquaCore::Network::Esp32NetworkBackend
        networkBackend_;

    AquaCore::Network::NetworkService
        networkService_;


    // =========================================================
    // WEB
    // =========================================================

    HydroSenseDashboard dashboard_;
    HydroSenseApi api_;

    HydroSenseSettingsPage
        settingsPage_;

    HydroSenseSettingsApi
        settingsApi_;

    HydroSenseControlPage
        controlPage_;

    HydroSenseControlApi
        controlApi_;

    HydroSenseDiagnosticsPage
        diagnosticsPage_;

    AquaCore::Web::Esp32WebBackend
        webBackend_;

    AquaCore::Web::WebService
        webService_;


    // =========================================================
    // RESTART
    // =========================================================

    bool restartPending_ = false;

    uint32_t restartRequestedMs_ = 0;
};