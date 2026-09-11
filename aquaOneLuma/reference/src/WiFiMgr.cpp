#include "WiFiMgr.h"
#include "RuntimeConfig.h"
#include <Arduino.h>
#include <WiFi.h>

#include "DebugLog.h"
namespace {
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_DEFAULT_MS = 5000;
constexpr uint32_t WIFI_RETRY_FAST_MS = 1000;
constexpr uint32_t WIFI_RETRY_AUTH_FAIL_MS = 10000;
constexpr uint32_t WIFI_RETRY_MAX_MS = 10000;

bool hasWiFiConfiguration(const RuntimeConfig& config) {
  return config.wifiSsid[0] != '\0';
}
}

WiFiMgr* WiFiMgr::instance_ = nullptr;

WiFiMgr::WiFiMgr(const RuntimeConfig& config) : config_(config) {}

void WiFiMgr::begin() {
  instance_ = this;
  WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  startConnect();
}

void WiFiMgr::loop() {
  if (sleepShutdownActive_) return;
  const uint32_t now = millis();

  // Callback Wi-Fi zapisuje tylko zdarzenie pod portMUX; decyzja o reconnect następuje w loop().
  PendingDisconnectEvent disconnectEvent{};
  portENTER_CRITICAL(&eventMux_);
  if (pendingDisconnect_.pending) {
    disconnectEvent = pendingDisconnect_;
    pendingDisconnect_.pending = false;
  }
  portEXIT_CRITICAL(&eventMux_);

  if (disconnectEvent.pending) {
    DEBUG_LOGF("[WIFI][%lu] DISCONNECTED reason=%u\n", disconnectEvent.timestamp,
               disconnectEvent.reason);
    if (WiFi.status() != WL_CONNECTED) {
      scheduleRetry(disconnectEvent.reason, now);
    } else {
      DEBUG_LOGF("[WIFI][%lu] stale disconnect event ignored\n", now);
    }
  }

  if (state_ == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = WIFI_CONNECTED;
      consecutiveFailureCount_ = 0;
      lastFailureReason_ = 0;
      assocComebackFastRetryCount_ = 0;
      portENTER_CRITICAL(&eventMux_);
      pendingDisconnect_ = {};
      portEXIT_CRITICAL(&eventMux_);
      DEBUG_LOGF("[WIFI][%lu] WIFI_CONNECTED in %lu ms, IP %s\n", now,
                    now - connectStartedAt_, WiFi.localIP().toString().c_str());
      return;
    }
    if (now - connectStartedAt_ >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.printf("[WIFI][%lu] connection timeout after %lu ms\n", now,
                    WIFI_CONNECT_TIMEOUT_MS);
      scheduleRetry(0, now);
    }
    return;
  }

  if (state_ == WIFI_CONNECTED && WiFi.status() != WL_CONNECTED) {
    DEBUG_LOGF("[WIFI][%lu] disconnected without reason event\n", now);
    scheduleRetry(0, now);
    return;
  }

  if (state_ == WIFI_ERROR && static_cast<int32_t>(now - retryAt_) >= 0) {
    DEBUG_LOGF("[WIFI][%lu] WIFI_RETRY\n", now);
    startConnect();
  }
}

void WiFiMgr::prepareForSleepShutdown() {
  sleepShutdownActive_ = true;
  portENTER_CRITICAL(&eventMux_);
  pendingDisconnect_ = {};
  portEXIT_CRITICAL(&eventMux_);
  state_ = WIFI_IDLE;
}
bool WiFiMgr::isConnected() const {
  return state_ == WIFI_CONNECTED && WiFi.status() == WL_CONNECTED;
}

WiFiState WiFiMgr::state() const {
  return state_;
}

void WiFiMgr::startConnect() {
  const uint32_t now = millis();
  if (!hasWiFiConfiguration(config_)) {
    state_ = WIFI_ERROR;
    retryAt_ = now + WIFI_RETRY_DEFAULT_MS;
    Serial.printf("[WIFI][%lu] configuration missing; enter CONFIG MODE and save configuration\n", now);
    return;
  }

  if (config_.useStaticIp) {
    IPAddress staticIp;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
    if (!staticIp.fromString(config_.staticIp) || !gateway.fromString(config_.gatewayIp) ||
        !subnet.fromString(config_.subnetMask) || !dns1.fromString(config_.dns1Ip) ||
        !dns2.fromString(config_.dns2Ip) || !WiFi.config(staticIp, gateway, subnet, dns1, dns2)) {
      Serial.printf("[WIFI][%lu] static IP configuration failed; using DHCP\n", now);
    }
  }
  if (config_.wifiPass[0] == '\0') {
    WiFi.begin(config_.wifiSsid);
  } else {
    WiFi.begin(config_.wifiSsid, config_.wifiPass);
  }
  state_ = WIFI_CONNECTING;
  connectStartedAt_ = now;
  DEBUG_LOGF("[WIFI][%lu] WIFI_START connecting...\n", now);
}

void WiFiMgr::scheduleRetry(uint8_t reason, uint32_t now) {
  const uint32_t retryDelay = retryDelayForReason(reason);
  state_ = WIFI_ERROR;
  retryAt_ = now + retryDelay;
  if (reason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG &&
      retryDelay == WIFI_RETRY_FAST_MS) {
    DEBUG_LOGF("[WIFI][%lu] reason=208 fast retry %u/3 in 1000 ms\n", now,
                  assocComebackFastRetryCount_);
  }
  DEBUG_LOGF("[WIFI][%lu] retry scheduled in %lu ms\n", now, retryDelay);
}

uint32_t WiFiMgr::retryDelayForReason(uint8_t reason) {
  if (reason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG) {
    if (assocComebackFastRetryCount_ < 3) {
      ++assocComebackFastRetryCount_;
      return WIFI_RETRY_FAST_MS;
    }
    return WIFI_RETRY_DEFAULT_MS;
  }

  assocComebackFastRetryCount_ = 0;
  if (reason == lastFailureReason_) {
    if (consecutiveFailureCount_ < 5) {
      ++consecutiveFailureCount_;
    }
  } else {
    lastFailureReason_ = reason;
    consecutiveFailureCount_ = 1;
  }

  uint32_t baseDelay = WIFI_RETRY_DEFAULT_MS;
  switch (reason) {
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_TOOMANY:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
      baseDelay = WIFI_RETRY_FAST_MS;
      break;
    case WIFI_REASON_NO_AP_FOUND:
      baseDelay = WIFI_RETRY_DEFAULT_MS;
      break;
    case WIFI_REASON_AUTH_FAIL:
      baseDelay = WIFI_RETRY_AUTH_FAIL_MS;
      break;
    default:
      break;
  }

  uint32_t delay = baseDelay;
  for (uint8_t attempt = 1; attempt < consecutiveFailureCount_ && delay < WIFI_RETRY_MAX_MS; ++attempt) {
    delay *= 2;
  }
  return delay > WIFI_RETRY_MAX_MS ? WIFI_RETRY_MAX_MS : delay;
}

void WiFiMgr::onWiFiEvent(arduino_event_id_t event, WiFiEventInfo_t info) {
  if (!instance_ || event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    return;
  }

  portENTER_CRITICAL(&instance_->eventMux_);
  instance_->pendingDisconnect_.reason = info.wifi_sta_disconnected.reason;
  instance_->pendingDisconnect_.timestamp = millis();
  instance_->pendingDisconnect_.pending = true;
  portEXIT_CRITICAL(&instance_->eventMux_);
}
