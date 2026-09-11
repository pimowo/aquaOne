#pragma once

#include <Arduino.h>

constexpr uint8_t YORADIO_MAX = 9;

struct YoRadioConfig {
  uint8_t id;
  const char* name;
  const char* host;
  uint16_t wsPort;
  const char* wsPath;
  bool enabled;
};

enum RadioConnectionState {
  RADIO_DISABLED,
  RADIO_OFFLINE_WIFI,
  RADIO_WS_CONNECTING,
  RADIO_ONLINE,
  RADIO_OFFLINE,
  RADIO_WS_ERROR,
};
