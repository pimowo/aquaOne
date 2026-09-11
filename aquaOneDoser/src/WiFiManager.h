#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WiFiManager {
public:
    void begin();
    void loop();
    bool isConnected() const;

private:
    enum class State : uint8_t { IDLE, CONNECTING, CONNECTED, WAIT_RETRY };

    State state = State::IDLE;
    unsigned long attemptStartedAt = 0;
    unsigned long retryStartedAt = 0;
    unsigned long retryDelayMs = 0;
    uint16_t attemptCount = 0;
    unsigned long bootStartedAt = 0;
    wl_status_t lastDriverStatus = WL_NO_SHIELD;
    volatile bool staStartedEvent = false;
    volatile bool staConnectedEvent = false;
    volatile bool gotIpEvent = false;
    volatile bool disconnectedEvent = false;
    volatile uint8_t disconnectReason = 0;
    volatile bool terminalAssociationEvent = false;
    volatile uint8_t terminalAssociationReason = 0;

    static WiFiManager* instance;
    static void onEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    void startInitialConnection();
    void startReconnect();
    void processEvents();
    void logDriverStatus(wl_status_t status);
    static const char* statusName(wl_status_t status);
    static bool isTerminalAssociationReason(uint8_t reason);
    void enterRetry(unsigned long delayMs);
};