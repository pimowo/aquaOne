#pragma once
#include <Arduino.h>
#include <WiFi.h>

struct RuntimeConfig;

enum WiFiState {
  WIFI_IDLE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_ERROR,
};

class WiFiMgr {
public:
  explicit WiFiMgr(const RuntimeConfig& config);
  void begin();
  void loop();
  void prepareForSleepShutdown();

  bool isConnected() const;
  WiFiState state() const;

private:
  void startConnect();
  void scheduleRetry(uint8_t reason, uint32_t now);
  uint32_t retryDelayForReason(uint8_t reason);
  static void onWiFiEvent(arduino_event_id_t event, WiFiEventInfo_t info);

  struct PendingDisconnectEvent {
    bool pending{false};
    uint8_t reason{0};
    uint32_t timestamp{0};
  };

  WiFiState state_{WIFI_IDLE};
  bool sleepShutdownActive_{false};
  uint32_t connectStartedAt_{0};
  uint32_t retryAt_{0};
  portMUX_TYPE eventMux_ = portMUX_INITIALIZER_UNLOCKED;
  PendingDisconnectEvent pendingDisconnect_{};
  uint8_t lastFailureReason_{0};
  uint8_t consecutiveFailureCount_{0};
  uint8_t assocComebackFastRetryCount_{0};
  static WiFiMgr* instance_;
  const RuntimeConfig& config_;
};
