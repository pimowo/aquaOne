#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Network {

constexpr size_t WIFI_SSID_CAPACITY = 33U;
constexpr size_t WIFI_PASSWORD_CAPACITY = 65U;
constexpr size_t WIFI_HOSTNAME_CAPACITY = 33U;
constexpr uint32_t DEFAULT_RECONNECT_INTERVAL_MS = 10000U;

struct NetworkConfig {
    bool staEnabled = false;
    char ssid[WIFI_SSID_CAPACITY] {};
    char password[WIFI_PASSWORD_CAPACITY] {};
    char hostname[WIFI_HOSTNAME_CAPACITY] {};
    bool autoReconnect = true;
    uint32_t reconnectIntervalMs =
        DEFAULT_RECONNECT_INTERVAL_MS;

    bool apEnabled = false;
    char apSsid[WIFI_SSID_CAPACITY] {};
    char apPassword[WIFI_PASSWORD_CAPACITY] {};
};

} // namespace Network
} // namespace AquaCore
