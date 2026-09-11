#pragma once

#include <Arduino.h>
#include "ConfigDefaults.h"
#include "RadioTypes.h"

constexpr size_t RUNTIME_WIFI_SSID_LEN = 33;
constexpr size_t RUNTIME_WIFI_PASS_LEN = 65;
constexpr size_t RUNTIME_IP_LEN = 16;
constexpr size_t RUNTIME_RADIO_NAME_LEN = 33;
constexpr size_t RUNTIME_RADIO_HOST_LEN = 65;
constexpr size_t RUNTIME_RADIO_PATH_LEN = 33;
constexpr size_t RUNTIME_TIMEZONE_LEN = 64;
constexpr size_t RUNTIME_NTP_SERVER_LEN = 65;

struct RuntimeRadioConfig {
  uint8_t id{0};
  bool enabled{false};
  char name[RUNTIME_RADIO_NAME_LEN]{};
  char host[RUNTIME_RADIO_HOST_LEN]{};
  uint16_t wsPort{ConfigDefaults::kWebSocketPort};
  char wsPath[RUNTIME_RADIO_PATH_LEN]{"/ws"};
};

struct RuntimeConfig {
  char wifiSsid[RUNTIME_WIFI_SSID_LEN]{};
  char wifiPass[RUNTIME_WIFI_PASS_LEN]{};
  bool useStaticIp{false};
  char staticIp[RUNTIME_IP_LEN]{};
  char gatewayIp[RUNTIME_IP_LEN]{};
  char subnetMask[RUNTIME_IP_LEN]{};
  char dns1Ip[RUNTIME_IP_LEN]{};
  char dns2Ip[RUNTIME_IP_LEN]{};
  char timezone[RUNTIME_TIMEZONE_LEN]{};
  char ntpServer[RUNTIME_NTP_SERVER_LEN]{};
  RuntimeRadioConfig radios[YORADIO_MAX]{};
  uint32_t volumeScreenTimeoutMs{ConfigDefaults::kVolumeScreenTimeoutMs};
  uint32_t volumeRepeatMs{ConfigDefaults::kVolumeRepeatMs};
  uint32_t scrollStartDelayMs{ConfigDefaults::kScrollStartDelayMs};
  uint32_t scrollStepMs{ConfigDefaults::kScrollStepMs};
  uint32_t radioMenuTimeoutMs{ConfigDefaults::kRadioMenuTimeoutMs};
  uint32_t radioConnectingMinMs{ConfigDefaults::kRadioConnectingMinMs};
  uint32_t deepSleepTimeoutSec{ConfigDefaults::kDeepSleepTimeoutSec};
  uint32_t stoppedTimeoutSec{ConfigDefaults::kStoppedTimeoutSec};
};
