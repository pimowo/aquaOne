#pragma once

#include <AquaCore/System/SystemService.h>
#include <AquaCore/System/DeviceIdentity.h>
#include <AquaCore/System/Esp32FactoryDeviceIdSource.h>

#include <AquaCore/Network/Esp32NetworkBackend.h>
#include <AquaCore/Network/NetworkService.h>

#include <AquaCore/Web/EspIdfWebTransport.h>
#include <AquaCore/Web/NativeWebService.h>
#include <AquaCore/Web/Esp32SnapshotSynchronizer.h>
#include <AquaCore/Web/Esp32ActionBridgeSynchronizer.h>
#include <AquaCore/Web/CoreWebProjectionSources.h>
#include <AquaCore/Web/CoreWebProjectionPublisher.h>

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

#include "web/HydroNativeWeb.h"
#include "web/HydroWebApplication.h"

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
    AquaCore::Identity::DeviceIdentity canonicalIdentity_;
    AquaCore::Identity::Esp32FactoryDeviceIdSource factoryDeviceIdSource_;


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

    AquaCore::Web::Esp32SnapshotSynchronizer snapshotSynchronizer_;
    AquaCore::Web::PublishedSnapshot<AquaCore::Web::CoreSystemProjection> coreSystemSnapshot_;
    AquaCore::Web::PublishedSnapshot<AquaCore::Web::CoreDiagnosticsProjection> coreDiagnosticsSnapshot_;
    AquaCore::Web::PublishedSnapshot<SystemStatus> statusSnapshot_;
    AquaCore::Web::PublishedSnapshot<HydroSettingsProjection> settingsSnapshot_;
    AquaCore::Web::PublishedSnapshot<HydroDiagnosticsProjection> hydroDiagnosticsSnapshot_;

    AquaCore::Web::SystemServiceWebProjectionSource systemProjectionSource_;
    HydroCoreDiagnosticsSource coreDiagnosticsSource_;
    AquaCore::Web::CoreWebProjectionPublisher coreProjectionPublisher_;

    AquaCore::Web::Esp32ActionBridgeSynchronizer actionSynchronizer_;
    HydroApplicationBridge applicationBridge_;
    HydroWebApplication webApplication_;

    AquaCore::Web::EspIdfWebTransport webTransport_;
    AquaCore::Web::NativeWebService webService_;
    HydroNativeWeb nativeWeb_;


    // =========================================================
    // RESTART
    // =========================================================

    bool restartPending_ = false;

    uint32_t restartRequestedMs_ = 0;
};
