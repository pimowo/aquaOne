#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Network {

constexpr size_t WIFI_SSID_CAPACITY = 33U;
constexpr size_t WIFI_PASSWORD_CAPACITY = 65U;
constexpr size_t WIFI_HOSTNAME_CAPACITY = 33U;
constexpr uint32_t DEFAULT_RECONNECT_INTERVAL_MS = 10000U;

enum class TriStateSetting : uint8_t {
    FrameworkDefault = 0U,
    Enabled,
    Disabled
};

enum class WifiPowerSaveMode : uint8_t {
    FrameworkDefault = 0U,
    None
};

struct NetworkConfig {
    bool staEnabled = false;
    char ssid[WIFI_SSID_CAPACITY] {};
    char password[WIFI_PASSWORD_CAPACITY] {};
    char hostname[WIFI_HOSTNAME_CAPACITY] {};
    bool autoReconnect = true;
    uint32_t reconnectIntervalMs =
        DEFAULT_RECONNECT_INTERVAL_MS;
    uint32_t connectTimeoutMs = 0U;
    bool fastRetryEnabled = false;
    uint32_t fastReconnectIntervalMs = 0U;

    TriStateSetting persistent = TriStateSetting::FrameworkDefault;
    TriStateSetting sdkAutoReconnect = TriStateSetting::FrameworkDefault;
    WifiPowerSaveMode powerSave = WifiPowerSaveMode::FrameworkDefault;

    bool apEnabled = false;
    char apSsid[WIFI_SSID_CAPACITY] {};
    char apPassword[WIFI_PASSWORD_CAPACITY] {};
};

} // namespace Network
} // namespace AquaCore
