#include "WSClient.h"

#include "DataStore.h"
#include "WiFiMgr.h"
#include <Arduino.h>
#include <WebSocketsClient.h>

#include "DebugLog.h"
namespace {
// Dwa sloty izolują callbacki poprzedniej sesji od aktualnego po zmianie radia.
constexpr uint8_t WS_SOCKET_SLOT_COUNT = 2;
constexpr uint32_t RADIO_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t WS_RECONNECT_RETRY_INTERVAL_MS = 500;
WebSocketsClient webSockets[WS_SOCKET_SLOT_COUNT];
constexpr size_t MAX_UNSUPPORTED_PAYLOAD_LOG_BYTES = 256;

void logInvalidJsonPayload(const uint8_t* payload, size_t length, uint32_t timestamp) {
#if defined(DEBUG_UART) && DEBUG_UART
  const size_t bytesToPrint = length < MAX_UNSUPPORTED_PAYLOAD_LOG_BYTES
                                  ? length
                                  : MAX_UNSUPPORTED_PAYLOAD_LOG_BYTES;
  DEBUG_LOGF("[DATA][%lu] invalid JSON payload (%u B): ", timestamp,
             static_cast<unsigned>(length));
  DEBUG_LOGWRITE(payload, bytesToPrint);
  if (bytesToPrint != length) {
    DEBUG_LOGF(" ... [truncated after %u B]", static_cast<unsigned>(bytesToPrint));
  }
  DEBUG_LOGLN();
#else
  (void)payload;
  Serial.printf("[DATA][%lu] invalid JSON payload (%u B)\n", timestamp,
                static_cast<unsigned>(length));
#endif
}
}  // namespace

WSClient::WSClient(WiFiMgr& wifi, DataStore& dataStore)
    : wifi_(wifi), dataStore_(dataStore) {}

void WSClient::begin() {}

void WSClient::connect(const RuntimeRadioConfig& config, uint32_t generation) {
  if (sleepShutdownActive_) {
    return;
  }

  disconnect();
  resetReconnectRetryCadence();
  config_ = &config;
  radioGeneration_ = generation;
  firstDataReceived_ = false;
  connectAttemptActive_ = false;
  connectionAttemptSucceeded_ = false;
  connectionAttemptAborted_ = false;
  connectAttemptStartedAt_ = 0;
  stopRuntimeReconnect();
  connectionState_ = RADIO_OFFLINE_WIFI;
  DEBUG_LOGF("[WS][%lu] selected radio %u: %s (radio generation %lu)\n", millis(),
             config.id, config.name, radioGeneration_);
}

// Numer sesji unieważnia opóźnione callbacki starego połączenia WebSocket.
void WSClient::invalidateSession() {
  if (++sessionId_ == 0) ++sessionId_;
}

void WSClient::disconnect() {
  if (started_) {
    started_ = false;
    webSockets[activeSocketSlot_].disconnect();
  }
  config_ = nullptr;
  firstDataReceived_ = false;
  connectAttemptActive_ = false;
  connectionAttemptSucceeded_ = false;
  connectionAttemptAborted_ = false;
  connectAttemptStartedAt_ = 0;
  stopRuntimeReconnect();
  invalidateSession();
  connectionState_ = RADIO_DISABLED;
}

void WSClient::prepareForSleepShutdown() {
  sleepShutdownActive_ = true;
}
void WSClient::startSession() {
  activeSocketSlot_ = nextSocketSlot_;
  nextSocketSlot_ = static_cast<uint8_t>((nextSocketSlot_ + 1) % WS_SOCKET_SLOT_COUNT);
  invalidateSession();

  if (!connectionAttemptSucceeded_ && !connectAttemptActive_) {
    connectAttemptActive_ = true;
    connectAttemptStartedAt_ = millis();
  }

  firstDataReceived_ = false;
  dataStore_.clearSessionData();
  startedAt_ = millis();
  lastSessionStartedAt_ = startedAt_;
  hasSessionStartedAt_ = true;
  connectionState_ = RADIO_WS_CONNECTING;
  started_ = true;

  WebSocketsClient& socket = webSockets[activeSocketSlot_];
  socket.disconnect();
  const uint32_t callbackSessionId = sessionId_;
  const uint8_t callbackSlot = activeSocketSlot_;
  socket.onEvent([this, callbackSessionId, callbackSlot](WStype_t type, uint8_t* payload,
                                                          size_t length) {
    handleEvent(static_cast<uint8_t>(type), payload, length, callbackSessionId, callbackSlot);
  });
  socket.begin(config_->host, config_->wsPort, config_->wsPath);
  socket.setReconnectInterval(5000);
  DEBUG_LOGF("[WS][%lu] WS_START connecting... (session %lu)\n", startedAt_, sessionId_);
}

// Sukces wyboru radia wymaga FIRST_DATA; samo połączenie WS nie kończy próby.
bool WSClient::connectionAttemptTimedOut(uint32_t now) const {
  return connectAttemptActive_ && !firstDataReceived_ && connectAttemptStartedAt_ != 0 &&
         static_cast<uint32_t>(now - connectAttemptStartedAt_) >= RADIO_CONNECT_TIMEOUT_MS;
}

bool WSClient::runtimeReconnectTimedOut(uint32_t now) const {
  return runtimeReconnectActive_ && !firstDataReceived_ &&
         static_cast<uint32_t>(now - runtimeReconnectStartedAt_) >= RADIO_CONNECT_TIMEOUT_MS;
}

bool WSClient::reconnectRetryDue(uint32_t now) const {
  return !hasSessionStartedAt_ ||
         static_cast<uint32_t>(now - lastSessionStartedAt_) >= WS_RECONNECT_RETRY_INTERVAL_MS;
}

void WSClient::resetReconnectRetryCadence() {
  lastSessionStartedAt_ = 0;
  hasSessionStartedAt_ = false;
}

void WSClient::startRuntimeReconnect(uint32_t now) {
  if (runtimeReconnectActive_) return;
  runtimeReconnectActive_ = true;
  runtimeReconnectStartedAt_ = now;
  DEBUG_LOGF("[WS][%lu] runtime reconnect window started\n", now);
}

void WSClient::stopRuntimeReconnect() {
  runtimeReconnectActive_ = false;
  runtimeReconnectStartedAt_ = 0;
}

void WSClient::abortConnectionAttempt() {
  const uint32_t now = millis();
  connectAttemptActive_ = false;
  stopRuntimeReconnect();
  connectionAttemptAborted_ = true;
  firstDataReceived_ = false;
  invalidateSession();
  if (started_) {
    started_ = false;
    webSockets[activeSocketSlot_].disconnect();
  }
  connectionState_ = RADIO_OFFLINE;
  DEBUG_LOGF("[WS][%lu] RADIO_OFFLINE: no FIRST_DATA within %lu ms\n", now,
             RADIO_CONNECT_TIMEOUT_MS);
}

void WSClient::loop() {
  if (sleepShutdownActive_ || !config_) return;
  if (connectionAttemptAborted_) return;

  const uint32_t now = millis();
  if (connectionAttemptTimedOut(now)) {
    abortConnectionAttempt();
    return;
  }

  if (!wifi_.isConnected()) {
    if (runtimeReconnectActive_) {
      stopRuntimeReconnect();
      DEBUG_LOGF("[WS][%lu] runtime reconnect paused: WiFi unavailable\n", now);
    }
    if (started_) {
      started_ = false;
      webSockets[activeSocketSlot_].disconnect();
      DEBUG_LOGF("[WS][%lu] stopped: WiFi unavailable\n", now);
    }
    resetReconnectRetryCadence();
    connectionState_ = RADIO_OFFLINE_WIFI;
    return;
  }

  if (runtimeReconnectTimedOut(now)) {
    abortConnectionAttempt();
    return;
  }

  if (!started_) {
    if (config_->host[0] == '\0' || config_->wsPath[0] == '\0') {
      connectionState_ = RADIO_WS_ERROR;
      Serial.printf("[WS][%lu] invalid active radio configuration\n", now);
      return;
    }
    if (connectionAttemptSucceeded_) startRuntimeReconnect(now);
    if (!reconnectRetryDue(now)) return;
    startSession();
  }

  webSockets[activeSocketSlot_].loop();
  if (connectionAttemptTimedOut(millis()) || runtimeReconnectTimedOut(millis())) {
    abortConnectionAttempt();
  }
}

void WSClient::handleEvent(uint8_t type, uint8_t* payload, size_t length,
                           uint32_t callbackSessionId, uint8_t callbackSlot) {
  if (sleepShutdownActive_ || !config_ || !started_ || callbackSlot != activeSocketSlot_ ||
      callbackSessionId != sessionId_) {
    DEBUG_LOGF("[WS][%lu] stale event dropped\n", millis());
    return;
  }

  const uint32_t now = millis();
  switch (static_cast<WStype_t>(type)) {
    case WStype_CONNECTED:
      connectionState_ = RADIO_ONLINE;
      DEBUG_LOGF("[WS][%lu] WS_CONNECTED in %lu ms\n", now, now - startedAt_);
      webSockets[activeSocketSlot_].sendTXT("getindex=1");
      DEBUG_LOGF("[WS][%lu] GETINDEX_SENT\n", now);
      break;
    case WStype_DISCONNECTED:
      started_ = false;
      firstDataReceived_ = false;
      connectionState_ = RADIO_WS_CONNECTING;
      DEBUG_LOGF("[WS][%lu] disconnected\n", now);
      break;
    case WStype_TEXT: {
      if (connectionState_ != RADIO_ONLINE) return;
      const DataUpdateResult updateResult = dataStore_.updateFromJson(payload, length);
      if (updateResult == DataUpdateResult::ACCEPTED) {
        if (!firstDataReceived_) {
          firstDataReceived_ = true;
          connectAttemptActive_ = false;
          connectionAttemptSucceeded_ = true;
          stopRuntimeReconnect();
          connectionAttemptAborted_ = false;
          DEBUG_LOGF("[DATA][%lu] FIRST_DATA\n", now);
        }
      } else if (updateResult == DataUpdateResult::INVALID_JSON) {
        logInvalidJsonPayload(payload, length, now);
      }
      break;
    }
    case WStype_ERROR:
      connectionState_ = RADIO_WS_ERROR;
      Serial.printf("[WS][%lu] error\n", now);
      break;
    default:
      break;
  }
}

RadioConnectionState WSClient::connectionState() const { return connectionState_; }

bool WSClient::sendCommand(const char* command) {
  if (connectionState_ != RADIO_ONLINE || command == nullptr || command[0] == '\0') {
    return false;
  }
  if (strcmp(command, "next=1") == 0 || strcmp(command, "prev=1") == 0) {
    dataStore_.clearMediaData();
  }
  webSockets[activeSocketSlot_].sendTXT(command);
  DEBUG_LOGF("[WS][%lu] command %s\n", millis(), command);
  return true;
}

bool WSClient::hasFirstData() const { return firstDataReceived_; }