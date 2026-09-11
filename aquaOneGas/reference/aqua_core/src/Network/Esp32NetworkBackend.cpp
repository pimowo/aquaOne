#include "AquaCore/Network/Esp32NetworkBackend.h"

#include <WiFi.h>

namespace AquaCore {
namespace Network {
namespace {

bool enableStaMode() {
    const wifi_mode_t current = WiFi.getMode();

    if (
        current == WIFI_MODE_STA ||
        current == WIFI_MODE_APSTA
    ) {
        return true;
    }

    return WiFi.mode(
        current == WIFI_MODE_AP
            ? WIFI_AP_STA
            : WIFI_STA
    );
}

bool enableAccessPointMode() {
    const wifi_mode_t current = WiFi.getMode();

    if (
        current == WIFI_MODE_AP ||
        current == WIFI_MODE_APSTA
    ) {
        return true;
    }

    return WiFi.mode(
        current == WIFI_MODE_STA
            ? WIFI_AP_STA
            : WIFI_AP
    );
}

IpAddress copyAddress(const ::IPAddress& source) {
    IpAddress result {};

    for (uint8_t index = 0U; index < 4U; ++index) {
        result.octets[index] = source[index];
    }

    return result;
}

} // namespace

bool Esp32NetworkBackend::setHostname(
    const char* hostname
) {
    return
        hostname != nullptr &&
        WiFi.setHostname(hostname);
}

bool Esp32NetworkBackend::beginSta(
    const char* ssid,
    const char* password
) {
    if (ssid == nullptr || !enableStaMode()) {
        return false;
    }

    const wl_status_t result =
        WiFi.begin(
            ssid,
            password != nullptr ? password : ""
        );

    return result != WL_CONNECT_FAILED;
}

BackendStaState Esp32NetworkBackend::staState() const {
    switch (WiFi.status()) {
        case WL_CONNECTED:
            return BackendStaState::Connected;

        case WL_IDLE_STATUS:
        case WL_SCAN_COMPLETED:
            return BackendStaState::Connecting;

        case WL_DISCONNECTED:
        case WL_CONNECTION_LOST:
            return BackendStaState::Disconnected;

        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        default:
            return BackendStaState::Error;
    }
}

bool Esp32NetworkBackend::reconnectSta() {
    return WiFi.reconnect();
}

bool Esp32NetworkBackend::disconnectSta() {
    return WiFi.disconnect(false, false);
}

IpAddress Esp32NetworkBackend::localIp() const {
    return copyAddress(WiFi.localIP());
}

int32_t Esp32NetworkBackend::rssi() const {
    return static_cast<int32_t>(WiFi.RSSI());
}

bool Esp32NetworkBackend::startAccessPoint(
    const char* ssid,
    const char* password
) {
    if (ssid == nullptr || !enableAccessPointMode()) {
        return false;
    }

    if (password == nullptr || password[0] == '\0') {
        return WiFi.softAP(ssid);
    }

    return WiFi.softAP(ssid, password);
}

bool Esp32NetworkBackend::stopAccessPoint() {
    return WiFi.softAPdisconnect(false);
}

IpAddress Esp32NetworkBackend::accessPointIp() const {
    return copyAddress(WiFi.softAPIP());
}

} // namespace Network
} // namespace AquaCore
