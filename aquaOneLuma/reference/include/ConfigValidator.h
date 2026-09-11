#pragma once

#include <Arduino.h>

struct RuntimeConfig;

enum class ConfigValidationError : uint8_t {
  NONE,
  STRING_NOT_TERMINATED,
  SSID_MISSING,
  SSID_TOO_LONG,
  WIFI_PASSWORD_TOO_LONG,
  INVALID_STATIC_IP,
  INVALID_GATEWAY,
  INVALID_SUBNET,
  INVALID_DNS1,
  INVALID_DNS2,
  TIMEZONE_INVALID,
  NTP_SERVER_INVALID,
  VOLUME_TIMEOUT_OUT_OF_RANGE,
  VOLUME_REPEAT_OUT_OF_RANGE,
  SCROLL_DELAY_OUT_OF_RANGE,
  SCROLL_STEP_OUT_OF_RANGE,
  RADIO_MENU_TIMEOUT_OUT_OF_RANGE,
  RADIO_CONNECTING_TIMEOUT_OUT_OF_RANGE,
  DEEP_SLEEP_TIMEOUT_OUT_OF_RANGE,
  STOPPED_TIMEOUT_OUT_OF_RANGE,
  RADIO_ID_INVALID,
  RADIO_NAME_MISSING,
  RADIO_HOST_MISSING,
  RADIO_PORT_OUT_OF_RANGE,
  RADIO_PATH_INVALID,
  RADIO_ID_DUPLICATE,
  NO_ENABLED_RADIOS,
  FORM_FIELD_MISSING,
  FORM_NUMBER_INVALID,
};

enum class ValidationSection : uint8_t { NONE, NETWORK, RADIO, REMOTE };
enum class ValidationField : uint8_t { NONE, SSID, STATIC_IP, GATEWAY, SUBNET, DNS1, DNS2, TIMEZONE, NTP_SERVER, RADIO_ID, RADIO_NAME, RADIO_HOST, RADIO_PORT, RADIO_PATH, VOLUME_TIMEOUT, VOLUME_REPEAT, SCROLL_DELAY, SCROLL_STEP, RADIO_MENU_TIMEOUT, RADIO_CONNECTING_TIMEOUT, DEEP_SLEEP_TIMEOUT, STOPPED_TIMEOUT };

struct ValidationResult {
  bool ok{true};
  ConfigValidationError error{ConfigValidationError::NONE};
  ValidationSection section{ValidationSection::NONE};
  ValidationField field{ValidationField::NONE};
  uint8_t index{0};
};

namespace ConfigValidator {
constexpr uint8_t LCD_BRIGHTNESS_MIN_PERCENT = 0;
constexpr uint8_t LCD_BRIGHTNESS_MAX_PERCENT = 100;
constexpr uint32_t VOLUME_TIMEOUT_MIN_MS = 500;
constexpr uint32_t VOLUME_TIMEOUT_MAX_MS = 10000;
constexpr uint32_t VOLUME_REPEAT_MIN_MS = 50;
constexpr uint32_t VOLUME_REPEAT_MAX_MS = 2000;
constexpr uint32_t SCROLL_DELAY_MIN_MS = 250;
constexpr uint32_t SCROLL_DELAY_MAX_MS = 10000;
constexpr uint32_t SCROLL_STEP_MIN_MS = 10;
constexpr uint32_t SCROLL_STEP_MAX_MS = 1000;
constexpr uint32_t RADIO_MENU_TIMEOUT_MIN_MS = 500;
constexpr uint32_t RADIO_MENU_TIMEOUT_MAX_MS = 10000;
constexpr uint32_t RADIO_CONNECTING_MIN_MS = 500;
constexpr uint32_t RADIO_CONNECTING_MAX_MS = 10000;
constexpr uint32_t DEEP_SLEEP_TIMEOUT_MIN_SEC = 0;
constexpr uint32_t DEEP_SLEEP_TIMEOUT_MAX_SEC = 86400;
constexpr uint32_t STOPPED_TIMEOUT_MIN_SEC = 0;
constexpr uint32_t STOPPED_TIMEOUT_MAX_SEC = 86400;

bool isValidLcdBrightness(uint8_t brightnessPercent);
bool validate(const RuntimeConfig& config, ConfigValidationError& error, uint8_t& radioIndex);
bool validate(const RuntimeConfig& config, ValidationResult& result);
bool validateRadios(const RuntimeConfig& config, ConfigValidationError& error, uint8_t& radioIndex);
}
