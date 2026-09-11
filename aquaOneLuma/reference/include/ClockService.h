#pragma once

#include <Arduino.h>

class WiFiMgr;
struct RuntimeConfig;

class ClockService {
public:
  explicit ClockService(const RuntimeConfig& config);
  void loop(const WiFiMgr& wifi);
  bool isValid() const;
  void format(char* buffer, size_t length, bool colonVisible) const;

private:
  enum class NtpRequest : uint8_t {
    START,
    FAST_RETRY,
    NORMAL_RETRY
  };

  void startNtp(uint32_t now, NtpRequest request);

  const RuntimeConfig& config_;
  bool wifiConnected_{false};
  bool timeValidLogged_{false};
  bool waitingForTimeLogged_{false};
  uint8_t fastRetries_{0};
  uint32_t syncStartedAt_{0};
  uint32_t lastNtpRequestAt_{0};
};