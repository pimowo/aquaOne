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

NetworkDisconnectReason mapEspReason(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_ASSOC_EXPIRE:
            return NetworkDisconnectReason::AssociationExpired;
        case WIFI_REASON_CONNECTION_FAIL:
            return NetworkDisconnectReason::ConnectionFailed;
        case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
            return NetworkDisconnectReason::AssociationComebackTooLong;
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return NetworkDisconnectReason::AuthenticationFailed;
        case WIFI_REASON_NO_AP_FOUND:
            return NetworkDisconnectReason::ApNotFound;
        default:
            return reason != 0U
                ? NetworkDisconnectReason::Other
                : NetworkDisconnectReason::None;
    }
}

} // namespace

Esp32NetworkBackend* Esp32NetworkBackend::activeInstance_ = nullptr;

Esp32NetworkBackend::Esp32NetworkBackend() {
    activeInstance_ = this;
}

Esp32NetworkBackend::~Esp32NetworkBackend() {
    if (eventHandlerId_ != 0) {
        WiFi.removeEvent(eventHandlerId_);
        eventHandlerId_ = 0;
    }
    if (activeInstance_ == this) {
        activeInstance_ = nullptr;
    }
}

void Esp32NetworkBackend::ensureEventHandler() {
    activeInstance_ = this;
    if (eventHandlerId_ == 0) {
        eventHandlerId_ = static_cast<int>(WiFi.onEvent(
            [](WiFiEvent_t event, WiFiEventInfo_t info) {
                if (
                    activeInstance_ != nullptr &&
                    event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED
                ) {
                    activeInstance_->pendingEspReason_.store(
                        info.wifi_sta_disconnected.reason,
                        std::memory_order_relaxed
                    );
                    activeInstance_->hasPendingReason_.store(
                        true,
                        std::memory_order_release
                    );
                }
            },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED
        ));
    }
}

bool Esp32NetworkBackend::applyRadioPolicy(
    TriStateSetting persistent,
    TriStateSetting sdkAutoReconnect,
    WifiPowerSaveMode powerSave
) {
    ensureEventHandler();

    if (persistent == TriStateSetting::Enabled) {
        WiFi.persistent(true);
    } else if (persistent == TriStateSetting::Disabled) {
        WiFi.persistent(false);
    }

    if (sdkAutoReconnect == TriStateSetting::Enabled) {
        WiFi.setAutoReconnect(true);
    } else if (sdkAutoReconnect == TriStateSetting::Disabled) {
        WiFi.setAutoReconnect(false);
    }

    if (powerSave == WifiPowerSaveMode::None) {
        WiFi.setSleep(false);
    }

    return true;
}

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
    ensureEventHandler();

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

NetworkDisconnectReason Esp32NetworkBackend::consumeDisconnectReason() {
    if (!hasPendingReason_.exchange(false, std::memory_order_acq_rel)) {
        return NetworkDisconnectReason::None;
    }

    const uint8_t reason =
        pendingEspReason_.load(std::memory_order_relaxed);
    return mapEspReason(reason);
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
