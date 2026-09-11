#include "WiFiManager.h"

#include "app_config.h"
#include "secrets.h"

WiFiManager* WiFiManager::instance = nullptr;

void WiFiManager::begin() {
    instance = this;
    bootStartedAt = millis();
    WiFi.onEvent(onEvent);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    WiFi.setHostname(HOSTNAME);
    Serial.println("[WiFi] Auto reconnect: OFF (application state machine)");
    Serial.println("[WiFi] Power save: OFF");
    startInitialConnection();
}

void WiFiManager::startInitialConnection() {
    attemptCount = 1;
    attemptStartedAt = millis();
    state = State::CONNECTING;
    Serial.println("[WiFi] State: CONNECTING");
    Serial.println("[WiFi] Connection attempt: 1");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void WiFiManager::startReconnect() {
    ++attemptCount;
    attemptStartedAt = millis();
    terminalAssociationEvent = false;
    state = State::CONNECTING;
    Serial.println("[WiFi] State: CONNECTING");
    Serial.printf("[WiFi] Retry attempt: %u\n", attemptCount);
    if (!WiFi.reconnect()) {
        Serial.println("[WiFi] Reconnect start failed");
        enterRetry(WIFI_RETRY_INTERVAL_MS);
    }
}

void WiFiManager::loop() {
    processEvents();

    const wl_status_t driverStatus = WiFi.status();
    if (driverStatus != lastDriverStatus) {
        lastDriverStatus = driverStatus;
        logDriverStatus(driverStatus);
    }

    if (driverStatus == WL_CONNECTED) {
        if (state != State::CONNECTED) {
            state = State::CONNECTED;
            Serial.println("[WiFi] State: CONNECTED");
            Serial.printf("[WiFi] GOT_IP after %lu ms from boot (%lu ms current attempt)\n",
                          millis(), millis() - attemptStartedAt);
            Serial.printf("[WiFi] Connected on attempt: %u\n", attemptCount);
            Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
            Serial.print("[WiFi] RSSI: "); Serial.println(WiFi.RSSI());
        }
        return;
    }

    const unsigned long now = millis();
    if (state == State::CONNECTED) {
        enterRetry(WIFI_RETRY_INTERVAL_MS);
    } else if (state == State::CONNECTING &&
               now - attemptStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
        Serial.println("[WiFi] Connect timeout");
        enterRetry(WIFI_RETRY_INTERVAL_MS);
    } else if (state == State::WAIT_RETRY &&
               now - retryStartedAt >= retryDelayMs) {
        startReconnect();
    }
}

void WiFiManager::processEvents() {
    if (staStartedEvent) {
        staStartedEvent = false;
        Serial.println("[WiFi] Event: STA_START");
    }
    if (staConnectedEvent) {
        staConnectedEvent = false;
        Serial.println("[WiFi] Event: STA_CONNECTED");
    }
    if (gotIpEvent) {
        gotIpEvent = false;
        Serial.println("[WiFi] Event: STA_GOT_IP");
    }
    if (disconnectedEvent) {
        disconnectedEvent = false;
        const uint8_t reason = disconnectReason;
        const char* name = WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
        if (name != nullptr && name[0] != '\0')
            Serial.printf("[WiFi] Event: STA_DISCONNECTED, reason: %u (%s)\n", reason, name);
        else
            Serial.printf("[WiFi] Event: STA_DISCONNECTED, reason: %u\n", reason);
    }
    if (terminalAssociationEvent) {
        terminalAssociationEvent = false;
        const uint8_t reason = terminalAssociationReason;
        if (state == State::CONNECTING) {
            const char* name = WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
            Serial.printf("[WiFi] Terminal association error: %u (%s)\n", reason,
                          name != nullptr && name[0] != '\0' ? name : "UNKNOWN");
            enterRetry(WIFI_FAST_RETRY_MS);
        }
    }
}

void WiFiManager::onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (instance == nullptr) return;
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            instance->staStartedEvent = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            instance->staConnectedEvent = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            instance->gotIpEvent = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            const uint8_t reason = info.wifi_sta_disconnected.reason;
            instance->disconnectReason = reason;
            instance->disconnectedEvent = true;
            if (isTerminalAssociationReason(reason)) {
                instance->terminalAssociationReason = reason;
                instance->terminalAssociationEvent = true;
            }
            break;
        }
        default:
            break;
    }
}

void WiFiManager::enterRetry(unsigned long delayMs) {
    if (state == State::WAIT_RETRY) return;
    retryStartedAt = millis();
    retryDelayMs = delayMs;
    state = State::WAIT_RETRY;
    Serial.println("[WiFi] State: WAIT_RETRY");
    if (delayMs == WIFI_FAST_RETRY_MS)
        Serial.printf("[WiFi] Fast retry in %lu ms\n", delayMs);
    else
        Serial.printf("[WiFi] Retry in %lu ms\n", delayMs);
}

bool WiFiManager::isTerminalAssociationReason(uint8_t reason) {
    return reason == WIFI_REASON_ASSOC_EXPIRE ||
           reason == WIFI_REASON_CONNECTION_FAIL ||
           reason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG;
}

void WiFiManager::logDriverStatus(wl_status_t status) {
    Serial.printf("[WiFi] Driver status: %s (%d)\n", statusName(status), static_cast<int>(status));
}

const char* WiFiManager::statusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE/CONNECTING";
        case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}