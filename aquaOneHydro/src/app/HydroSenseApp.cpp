#include "HydroSenseApp.h"

#include <Arduino.h>
#include <cstring>

#include "hardware/BoardPins.h"

HydroSenseApp::HydroSenseApp()
    : config_(),

      configStorage_(),

      pump_(
          BoardPins::PUMP
      ),

      floatSensor_(
          BoardPins::FLOAT_SENSOR
      ),

      ultrasonicSensor_(
          BoardPins::ULTRASONIC_TRIG,
          BoardPins::ULTRASONIC_ECHO
      ),

      button_(
          BoardPins::BUTTON
      ),

      buzzer_(
          BoardPins::BUZZER
      ),

      waterTank_(
          ultrasonicSensor_
      ),

      waterReserve_(
          waterTank_
      ),

      topupController_(
          pump_,
          floatSensor_
      ),

      alarmManager_(
          waterTank_,
          waterReserve_,
          topupController_
      ),

      buzzerController_(
          buzzer_,
          alarmManager_
      ),

      status_(),

      systemService_(),

      networkBackend_(),

      networkService_(
          networkBackend_
      ),

      snapshotSynchronizer_(),
      coreSystemSnapshot_(snapshotSynchronizer_),
      coreDiagnosticsSnapshot_(snapshotSynchronizer_),
      statusSnapshot_(snapshotSynchronizer_),
      settingsSnapshot_(snapshotSynchronizer_),
      hydroDiagnosticsSnapshot_(snapshotSynchronizer_),
      systemProjectionSource_(systemService_),
      coreDiagnosticsSource_(systemService_, networkService_, configStorage_),
      coreProjectionPublisher_(systemProjectionSource_, &coreDiagnosticsSource_,
                               coreSystemSnapshot_, coreDiagnosticsSnapshot_),
      actionSynchronizer_(),
      applicationBridge_(actionSynchronizer_),
      webApplication_(config_, configStorage_, topupController_, buzzerController_,
                      status_, systemService_, statusSnapshot_, settingsSnapshot_,
                      hydroDiagnosticsSnapshot_, applicationBridge_),
      webTransport_(),
      webService_(webTransport_, coreSystemSnapshot_, coreDiagnosticsSnapshot_),
      nativeWeb_(webService_, statusSnapshot_, settingsSnapshot_,
                 hydroDiagnosticsSnapshot_, applicationBridge_)
{
}

void HydroSenseApp::begin()
{
    // =========================================================
    // FAIL-SAFE
    // =========================================================

    pump_.begin();

    buzzer_.configure(true);
    buzzer_.begin();


    // =========================================================
    // SYSTEM
    // =========================================================

    beginSystem();


    // =========================================================
    // CONFIG
    // =========================================================

    loadConfiguration();


    // =========================================================
    // HYDROSENSE
    // =========================================================

    floatSensor_.configure(
        config_.floatActiveLow,
        config_.floatUsePullup,
        config_.floatDebounceMs
    );

    ultrasonicSensor_.configure(
        config_.ultrasonicMinDistanceCm,
        config_.ultrasonicMaxDistanceCm,
        config_.ultrasonicTimeoutUs
    );

    waterTank_.configure(
        config_.tankEmptyDistanceCm,
        config_.tankFullDistanceCm,
        config_.tankSampleIntervalMs,
        config_.tankMaxFailedSeries
    );

    waterReserve_.configure(
        config_.reserveLowPercent,
        config_.reserveCriticalPercent,
        config_.reserveHysteresisPercent
    );

    topupController_.configure(
        config_.topupStartDelayMs,
        config_.topupMaxPumpRuntimeMs
    );

    button_.configure(
        true,
        true,
        50,
        3000
    );


    // =========================================================
    // START LOKALNY
    // =========================================================

    floatSensor_.begin();
    ultrasonicSensor_.begin();
    button_.begin();

    waterTank_.begin();
    waterReserve_.begin();

    topupController_.begin();
    alarmManager_.begin();
    buzzerController_.begin();


    // =========================================================
    // NETWORK + WEB
    // =========================================================

    beginNetwork();

    updateStatus();

    coreProjectionPublisher_.update();
    webApplication_.publish();

    beginWeb();
}

void HydroSenseApp::update()
{
    const uint32_t now =
        millis();

    networkService_.update(
        now
    );

    button_.update();

    handleButton();

    waterTank_.update();

    waterReserve_.update();

    topupController_.setPumpAllowed(
        waterReserve_.allowsPump()
    );

    topupController_.update();

    alarmManager_.update();

    buzzerController_.update();

    updateStatus();

    if (webApplication_.processOne())
    {
        updateStatus();
    }

    coreProjectionPublisher_.update();
    webApplication_.publish();

    handleRestartRequest();
}

void HydroSenseApp::setServiceMode(
    bool enabled
)
{
    topupController_.setServiceMode(
        enabled
    );
}

bool HydroSenseApp::isServiceMode() const
{
    return
        topupController_
            .isServiceMode();
}

const SystemStatus&
HydroSenseApp::status() const
{
    return status_;
}

void HydroSenseApp::beginSystem()
{
    const AquaCore::DeviceIdentity identity(
        "hydrosense",
        "HydroSense",
        "0.1.0-dev",
        "ESP32-S3 Zero"
    );

    systemService_.begin(
        identity
    );
}

void HydroSenseApp::handleButton()
{
    if (button_.shortPress())
    {
        buzzerController_.mute();
    }

    if (button_.longPress())
    {
        setServiceMode(
            !isServiceMode()
        );
    }
}

void HydroSenseApp::handleRestartRequest()
{
    if (
        !restartPending_ &&
        webApplication_.restartRequested()
    )
    {
        webApplication_.clearRestartRequest();

        restartPending_ = true;

        restartRequestedMs_ =
            millis();

        pump_.off();
    }

    if (!restartPending_)
    {
        return;
    }

    if (
        millis() -
            restartRequestedMs_ <
        500
    )
    {
        return;
    }

    pump_.off();
    buzzer_.off();

    ESP.restart();
}

void HydroSenseApp::beginNetwork()
{
    AquaCore::Network::NetworkConfig
        networkConfig {};

    networkConfig.staEnabled =
        config_.wifiStaEnabled;

    std::strncpy(
        networkConfig.ssid,
        config_.wifiSsid,
        sizeof(networkConfig.ssid) - 1
    );

    std::strncpy(
        networkConfig.password,
        config_.wifiPassword,
        sizeof(networkConfig.password) - 1
    );

    std::strncpy(
        networkConfig.hostname,
        config_.wifiHostname,
        sizeof(networkConfig.hostname) - 1
    );

    networkConfig.autoReconnect =
        config_.wifiAutoReconnect;

    networkConfig.reconnectIntervalMs =
        config_.wifiReconnectIntervalMs;

    networkConfig.apEnabled =
        config_.wifiApEnabled;

    std::strncpy(
        networkConfig.apSsid,
        config_.wifiApSsid,
        sizeof(networkConfig.apSsid) - 1
    );

    std::strncpy(
        networkConfig.apPassword,
        config_.wifiApPassword,
        sizeof(networkConfig.apPassword) - 1
    );

    networkService_.begin(
        networkConfig
    );
}

void HydroSenseApp::beginWeb()
{
    if (!nativeWeb_.registerRoutes())
    {
        return;
    }

    AquaCore::Web::WebConfig
        webConfig {};

    webConfig.enabled = true;
    webConfig.port = 80;

    webConfig.navigationMask =
        AquaCore::Web::
            navigationSectionMask(
                AquaCore::Web::
                    NavigationSection::Dashboard
            ) |
        AquaCore::Web::
            navigationSectionMask(
                AquaCore::Web::
                    NavigationSection::Control
            ) |
        AquaCore::Web::
            navigationSectionMask(
                AquaCore::Web::
                    NavigationSection::Settings
            ) |
        AquaCore::Web::
            navigationSectionMask(
                AquaCore::Web::
                    NavigationSection::Diagnostics
            );

    webService_.begin(
        webConfig
    );
}

void HydroSenseApp::updateStatus()
{
    status_.serviceMode =
        topupController_.isServiceMode();

    status_.floatSensorActive =
        floatSensor_.isActive();

    status_.pumpOn =
        pump_.isOn();

    status_.pumpAllowed =
        topupController_.isPumpAllowed();

    status_.pumpLocked =
        topupController_.isLocked();

    status_.topupState =
        topupController_.state();

    status_.tankValid =
        waterTank_.isValid();

    status_.tankSensorFault =
        waterTank_.hasSensorFault();

    status_.tankDistanceCm =
        waterTank_.distanceCm();

    status_.tankLevelCm =
        waterTank_.levelCm();

    status_.tankLevelPercent =
        waterTank_.levelPercent();

    status_.reserveState =
        waterReserve_.state();

    status_.hasAlarm =
        alarmManager_.hasAlarm();

    status_.alarmCode =
        alarmManager_.code();

    status_.alarmSeverity =
        alarmManager_.severity();

    status_.buzzerMuted =
        buzzerController_.isMuted();

    status_.networkState =
        networkService_.state();

    status_.wifiConnected =
        networkService_.isConnected();

    status_.wifiRssi =
        networkService_.rssi();

    status_.ipAddress =
        networkService_.ipAddress();

    status_.accessPointActive =
        networkService_.isApActive();

    status_.accessPointIpAddress =
        networkService_
            .accessPointIpAddress();

    status_.networkReconnectCount =
        networkService_
            .reconnectCount();
}

void HydroSenseApp::loadConfiguration()
{
    if (!configStorage_.begin())
    {
        return;
    }

    HydroSenseConfig storedConfig {};

    if (
        configStorage_.load(
            storedConfig
        )
    )
    {
        config_ = storedConfig;

        return;
    }

    configStorage_.save(
        config_
    );
}
