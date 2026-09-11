#include "WiFiManager.h"

#include "app_config.h"
#include "secrets.h"

#include <cstring>

WiFiManager::WiFiManager()
    : networkService_(backend_) {
}

bool WiFiManager::begin() {
    AquaCore::Network::NetworkConfig config {};
    config.staEnabled = true;
    std::strncpy(config.ssid, WIFI_SSID, sizeof(config.ssid) - 1);
    std::strncpy(config.password, WIFI_PASS, sizeof(config.password) - 1);
    std::strncpy(config.hostname, HOSTNAME, sizeof(config.hostname) - 1);

    config.autoReconnect = true;
    config.reconnectIntervalMs = WIFI_RETRY_INTERVAL_MS;
    config.connectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
    config.fastRetryEnabled = true;
    config.fastReconnectIntervalMs = WIFI_FAST_RETRY_MS;

    config.persistent = AquaCore::Network::TriStateSetting::Disabled;
    config.sdkAutoReconnect = AquaCore::Network::TriStateSetting::Disabled;
    config.powerSave = AquaCore::Network::WifiPowerSaveMode::None;

    config.apEnabled = false;

    return networkService_.begin(config);
}

void WiFiManager::loop() {
    networkService_.update(millis());
}

bool WiFiManager::isConnected() const {
    return networkService_.isConnected();
}