#include <Arduino.h>

#include <cstring>

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Logging/Logger.h"
#include "AquaCore/Logging/SerialLogSink.h"
#include "AquaCore/Network/Esp32NetworkBackend.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/Esp32WebBackend.h"
#include "AquaCore/Web/WebService.h"

#include "app/FirmwareApp.h"
#include "BuildConfig.h"
#include "Constants.h"
#include "Version.h"

#include "storage/StorageService.h"
#include "time/NtpSyncCoordinator.h"
#include "time/TimeService.h"
#include "web/LumaWebApp.h"

#if __has_include("NetworkSecrets.h")
    #include "NetworkSecrets.h"
#else
    #include "NetworkSecrets.example.h"
#endif

#if LUMASENSE_HARDWARE == LUMASENSE_HW_AQMA
    #include "hardware/AqmaHardware.h"
#elif LUMASENSE_HARDWARE == LUMASENSE_HW_LOLIN32_TEST
    #include "hardware/Lolin32Hardware.h"
#else
    #error "Nieznana platforma LumaSense"
#endif

namespace {

LumaSense::StorageService storage;
LumaSense::TimeService timeService;
LumaSense::EspNtpBackend ntpBackend;
LumaSense::NtpService ntpService(
    timeService.rtcService(),
    ntpBackend
);
LumaSense::NtpSyncCoordinator ntpSyncCoordinator(ntpService);
AquaCore::SystemService systemService;
AquaCore::SerialLogSink serialLogSink(Serial);
AquaCore::Logger logger(serialLogSink);

#if LUMASENSE_HARDWARE == LUMASENSE_HW_AQMA
LumaSense::AqmaHardware hardware;
constexpr const char* HARDWARE_VARIANT = "AQMA";
#elif LUMASENSE_HARDWARE == LUMASENSE_HW_LOLIN32_TEST
LumaSense::Lolin32Hardware hardware;
constexpr const char* HARDWARE_VARIANT = "LOLIN32_TEST";
#endif

const AquaCore::DeviceIdentity DEVICE_IDENTITY(
    "lighting-controller",
    LUMASENSE_FIRMWARE_NAME,
    LUMASENSE_VERSION,
    HARDWARE_VARIANT
);

LumaSense::FirmwareApp app(
    hardware,
    storage,
    timeService
);

AquaCore::Network::Esp32NetworkBackend networkBackend;
AquaCore::Network::NetworkService networkService(networkBackend);

AquaCore::Diagnostics::DiagnosticsService diagnosticsService(
    systemService,
    timeService.rtcService(),
    storage.aquaService(),
    "Europe/Warsaw",
    &ntpService,
    &networkService
);

AquaCore::Web::Esp32WebBackend webBackend;
AquaCore::Web::WebService webService(
    webBackend,
    systemService,
    &diagnosticsService
);

LumaSense::Web::LumaWebApp lumaWebApp(
    webService,
    app,
    networkService,
    diagnosticsService
);

bool networkAddressReported = false;

void copyText(
    char* destination,
    size_t capacity,
    const char* source
) {
    if (destination == nullptr || capacity == 0U) {
        return;
    }

    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }

    std::strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

AquaCore::Network::NetworkConfig makeNetworkConfig() {
    AquaCore::Network::NetworkConfig config {};
    config.staEnabled = LUMASENSE_WIFI_ENABLED != 0;
    copyText(config.ssid, sizeof(config.ssid), LUMASENSE_WIFI_SSID);
    copyText(
        config.password,
        sizeof(config.password),
        LUMASENSE_WIFI_PASSWORD
    );
    copyText(
        config.hostname,
        sizeof(config.hostname),
        LUMASENSE_WIFI_HOSTNAME
    );
    config.autoReconnect = true;
    config.reconnectIntervalMs =
        AquaCore::Network::DEFAULT_RECONNECT_INTERVAL_MS;
    config.apEnabled = false;
    return config;
}

AquaCore::Web::WebConfig makeWebConfig() {
    using AquaCore::Web::NavigationSection;
    using AquaCore::Web::navigationSectionMask;

    AquaCore::Web::WebConfig config {};
    config.enabled = true;
    config.port = 80U;
    config.navigationMask =
        navigationSectionMask(NavigationSection::Dashboard) |
        navigationSectionMask(NavigationSection::Control) |
        navigationSectionMask(NavigationSection::Diagnostics) |
        navigationSectionMask(NavigationSection::System);
    return config;
}

const char* modeName(
    LumaSense::OperatingMode mode
) {
    switch (mode) {
        case LumaSense::OperatingMode::Normal:
            return "NORMAL";
        case LumaSense::OperatingMode::Service:
            return "SERVICE";
        case LumaSense::OperatingMode::Manual:
            return "MANUAL";
        case LumaSense::OperatingMode::ChannelTest:
            return "CHANNEL_TEST";
        case LumaSense::OperatingMode::Preview:
            return "PREVIEW";
        case LumaSense::OperatingMode::Simulation:
            return "SIMULATION";
        case LumaSense::OperatingMode::Off:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

void printIpAddress(
    const AquaCore::Network::IpAddress& address
) {
    Serial.print(address.octets[0]);
    Serial.print('.');
    Serial.print(address.octets[1]);
    Serial.print('.');
    Serial.print(address.octets[2]);
    Serial.print('.');
    Serial.print(address.octets[3]);
}

void printStartupStatus(
    bool networkOk,
    bool routesOk,
    bool webOk
) {
    const LumaSense::FirmwareStatus& status =
        app.status();
    const AquaCore::DeviceIdentity& identity =
        systemService.deviceIdentity();

    Serial.print(LUMASENSE_FIRMWARE_NAME);
    Serial.print(' ');
    Serial.println(LUMASENSE_VERSION);

    Serial.print("Aqua Core: ");
    Serial.println(systemService.aquaCoreVersion());

    Serial.print("Hardware variant: ");
    Serial.println(identity.hardwareVariant);

    Serial.print("Restart reason: ");
    Serial.println(
        AquaCore::restartReasonName(
            systemService.restartReason()
        )
    );

    Serial.print("Hardware: ");
    Serial.println(status.hardwareOk ? "OK" : "FAILED");

    Serial.print("RTC: ");
    Serial.println(status.rtcOk ? "OK" : "FAILED");

    Serial.print("Storage: ");
    Serial.println(status.storageOk ? "OK" : "FAILED");

    Serial.print("Config: ");
    Serial.println(
        status.configSource == LumaSense::ConfigSource::Nvs
            ? "NVS"
            : "DEFAULTS"
    );

    Serial.print("Core: ");
    Serial.println(status.coreOk ? "OK" : "FAILED");

    Serial.print("Time: ");
    Serial.println(status.timeValid ? "VALID" : "INVALID");

    Serial.print("Mode: ");
    Serial.println(modeName(app.state().mode));

    Serial.print("Network: ");
    if (!LUMASENSE_WIFI_ENABLED) {
        Serial.println("DISABLED (copy NetworkSecrets.example.h)");
    } else {
        Serial.println(networkOk ? "STARTING" : "FAILED");
    }

    Serial.print("Web routes: ");
    Serial.println(routesOk ? "OK" : "FAILED");

    Serial.print("Web server: ");
    Serial.println(webOk ? "READY" : "FAILED");

    if (LUMASENSE_WIFI_ENABLED && networkOk && webOk) {
        Serial.println("Web URL: waiting for Wi-Fi");
    }
}

void reportNetworkAddress() {
    if (!networkService.isConnected()) {
        networkAddressReported = false;
        return;
    }

    if (networkAddressReported) {
        return;
    }

    networkAddressReported = true;
    Serial.print("Wi-Fi connected, IP: ");
    printIpAddress(networkService.ipAddress());
    Serial.println();

    if (webService.isRunning()) {
        Serial.print("Web URL: http://");
        printIpAddress(networkService.ipAddress());
        Serial.println('/');
    }
}

#if LUMASENSE_DEBUG
void printRuntimeStatus(uint32_t nowMs) {
    static uint32_t lastPrintMs = 0;

    if (
        nowMs - lastPrintMs <
        LumaSense::DEBUG_STATUS_INTERVAL_MS
    ) {
        return;
    }

    lastPrintMs = nowMs;

    const LumaSense::RuntimeState& state =
        app.state();

    Serial.print("mode=");
    Serial.print(modeName(state.mode));
    Serial.print(" time=");
    Serial.print(state.timeValid ? "VALID" : "INVALID");
    Serial.print(" ch1_req=");
    Serial.print(state.requestedLevels.value[0], 2);
    Serial.print(" ch1_actual=");
    Serial.println(state.actualLevels.value[0], 2);
}

void printDateTime(
    const AquaCore::Time::LocalTime& value
) {
    if (!value.valid) {
        Serial.print("INVALID");
        return;
    }

    Serial.printf(
        "%04u-%02u-%02uT%02u:%02u:%02u",
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second
    );
}

void printTimeDiagnostics(uint32_t nowMs) {
    static uint32_t lastPrintMs = 0U;

    if (
        nowMs - lastPrintMs <
        LumaSense::TIME_DIAGNOSTICS_INTERVAL_MS
    ) {
        return;
    }

    lastPrintMs = nowMs;

    const AquaCore::Time::LocalTime rawUtc =
        timeService.rtcService().read();
    const LumaSense::LocalTime& local = app.localTime();
    uint32_t ntpAgeMs = 0U;
    const bool hasNtpAge =
        ntpService.lastSuccessfulSyncAgeMs(nowMs, ntpAgeMs);

    Serial.print("[TIME] millis=");
    Serial.print(nowMs);
    Serial.print(" rtcUtc=");
    printDateTime(rawUtc);
    Serial.print(" local=");
    printDateTime(local);
    Serial.print(" statusLocal=");
    printDateTime(local);
    Serial.print(" rtcReadAgeMs=");
    Serial.print(app.rtcReadAgeMs(nowMs));
    Serial.print(" ntp=");

    if (!ntpService.isInitialized()) {
        Serial.print("UNAVAILABLE");
    } else if (ntpService.isSyncInProgress()) {
        Serial.print("SYNCING");
    } else if (!ntpService.hasSyncResult()) {
        Serial.print("NOT_ATTEMPTED");
    } else {
        Serial.print(
            ntpService.lastSyncSucceeded()
                ? "SUCCESS"
                : "FAILURE"
        );
    }

    Serial.print(" ntpAgeMs=");
    if (hasNtpAge) {
        Serial.println(ntpAgeMs);
    } else {
        Serial.println('-');
    }
}
#endif

} // namespace

void setup() {
    const uint32_t nowMs = millis();

    (void)systemService.begin(DEVICE_IDENTITY);

    // Storage resolves PWM polarity before the only hardware.begin().
    // Network and Web are initialized only after the autonomous lamp.
    (void)app.begin(nowMs);

    Serial.begin(LUMASENSE_SERIAL_BAUD);
    logger.info("System", "autonomous core startup complete");

    const AquaCore::Network::NetworkConfig networkConfig =
        makeNetworkConfig();
    const bool networkOk =
        networkService.begin(networkConfig);
    const bool ntpOk = ntpSyncCoordinator.begin(nowMs);

    const bool routesOk =
        lumaWebApp.registerRoutes();

    const bool webOk =
        routesOk && webService.begin(makeWebConfig());

    printStartupStatus(networkOk, routesOk, webOk);
    Serial.print("NTP: ");
    Serial.println(ntpOk ? "READY" : "FAILED");
}

void loop() {
    const uint32_t nowMs = millis();

    // Lighting always runs before optional network services.
    (void)app.update(nowMs);

    networkService.update(nowMs);
    ntpSyncCoordinator.update(
        networkService.isConnected(),
        nowMs
    );
    lumaWebApp.update(nowMs);
    webService.update();

    reportNetworkAddress();

#if LUMASENSE_DEBUG
    printRuntimeStatus(nowMs);
    printTimeDiagnostics(nowMs);
#endif
}
