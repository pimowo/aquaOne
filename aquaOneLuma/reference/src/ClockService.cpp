#include "ClockService.h"

#include "WiFiMgr.h"
#include "RuntimeConfig.h"
#include <time.h>

#include "DebugLog.h"

namespace {
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t NTP_FAST_RETRY_FIRST_MS = 2000;
constexpr uint32_t NTP_FAST_RETRY_SECOND_MS = 4000;
constexpr uint8_t NTP_FAST_RETRY_COUNT = 2;
}

ClockService::ClockService(const RuntimeConfig& config) : config_(config) {}

void ClockService::loop(const WiFiMgr& wifi) {
  if (!wifi.isConnected()) {
    wifiConnected_ = false;
    return;
  }

  const uint32_t now = millis();
  if (!wifiConnected_) {
    wifiConnected_ = true;
    timeValidLogged_ = false;
    waitingForTimeLogged_ = false;
    fastRetries_ = 0;
    syncStartedAt_ = now;
    startNtp(now, NtpRequest::START);
    return;
  }

  if (isValid()) {
    if (!timeValidLogged_) {
      timeValidLogged_ = true;
      DEBUG_LOGF("[CLOCK][%lu] TIME_VALID after %lu ms\n", now,
                 static_cast<unsigned long>(now - syncStartedAt_));
    }
    return;
  }

  if (!waitingForTimeLogged_) {
    waitingForTimeLogged_ = true;
    DEBUG_LOGF("[CLOCK][%lu] WAITING_FOR_TIME\n", now);
  }

  if (fastRetries_ < NTP_FAST_RETRY_COUNT) {
    const uint32_t interval = fastRetries_ == 0 ? NTP_FAST_RETRY_FIRST_MS
                                                : NTP_FAST_RETRY_SECOND_MS;
    if (static_cast<uint32_t>(now - lastNtpRequestAt_) >= interval) {
      ++fastRetries_;
      startNtp(now, NtpRequest::FAST_RETRY);
    }
    return;
  }

  if (static_cast<uint32_t>(now - lastNtpRequestAt_) >= NTP_RETRY_INTERVAL_MS) {
    startNtp(now, NtpRequest::NORMAL_RETRY);
  }
}

void ClockService::startNtp(uint32_t now, NtpRequest request) {
  configTzTime(config_.timezone, config_.ntpServer);

  switch (request) {
    case NtpRequest::START:
      DEBUG_LOGF("[CLOCK][%lu] NTP_START\n", now);
      break;
    case NtpRequest::FAST_RETRY:
      DEBUG_LOGF("[CLOCK][%lu] FAST_RETRY\n", now);
      break;
    case NtpRequest::NORMAL_RETRY:
      DEBUG_LOGF("[CLOCK][%lu] NTP_RETRY\n", now);
      break;
  }
  lastNtpRequestAt_ = now;
}
bool ClockService::isValid() const {
  return time(nullptr) >= 1609459200;  // 2021-01-01: minimalna wiarygodna data zsynchronizowanego zegara.
}

void ClockService::format(char* buffer, size_t length, bool colonVisible) const {
  if (!isValid()) {
    snprintf(buffer, length, "--:--");
    return;
  }
  const time_t now = time(nullptr);
  const tm* local = localtime(&now);
  if (!local) {
    snprintf(buffer, length, "--:--");
    return;
  }
  snprintf(buffer, length, "%02d%c%02d", local->tm_hour, colonVisible ? ':' : ' ', local->tm_min);
}