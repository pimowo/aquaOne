#include "ConfigValidator.h"

#include "RuntimeConfig.h"

#include <IPAddress.h>
#include <string.h>

namespace {
template <size_t N>
bool terminated(const char (&value)[N]) {
  return memchr(value, '\0', N) != nullptr;
}

bool validIp(const char* value) {
  IPAddress address;
  return value && value[0] != '\0' && address.fromString(value);
}
}

bool ConfigValidator::isValidLcdBrightness(uint8_t brightnessPercent) {
  return brightnessPercent >= LCD_BRIGHTNESS_MIN_PERCENT &&
         brightnessPercent <= LCD_BRIGHTNESS_MAX_PERCENT;
}

bool ConfigValidator::validateRadios(const RuntimeConfig& config,
                                     ConfigValidationError& error,
                                     uint8_t& radioIndex) {
  bool enabledFound = false;
  radioIndex = 0;
  for (uint8_t i = 0; i < YORADIO_MAX; ++i) {
    const RuntimeRadioConfig& radio = config.radios[i];
    if (!terminated(radio.name) || !terminated(radio.host) || !terminated(radio.wsPath)) {
      error = ConfigValidationError::STRING_NOT_TERMINATED;
      radioIndex = i;
      return false;
    }
    if (!radio.enabled) continue;

    enabledFound = true;
    radioIndex = i;
    if (radio.id == 0 || radio.id > YORADIO_MAX) {
      error = ConfigValidationError::RADIO_ID_INVALID;
      return false;
    }
    if (radio.name[0] == '\0') {
      error = ConfigValidationError::RADIO_NAME_MISSING;
      return false;
    }
    if (radio.host[0] == '\0') {
      error = ConfigValidationError::RADIO_HOST_MISSING;
      return false;
    }
    if (radio.wsPort == 0) {
      error = ConfigValidationError::RADIO_PORT_OUT_OF_RANGE;
      return false;
    }
    if (radio.wsPath[0] == '\0' || radio.wsPath[0] != '/') {
      error = ConfigValidationError::RADIO_PATH_INVALID;
      return false;
    }
    for (uint8_t previous = 0; previous < i; ++previous) {
      if (config.radios[previous].enabled && config.radios[previous].id == radio.id) {
        error = ConfigValidationError::RADIO_ID_DUPLICATE;
        return false;
      }
    }
  }

  if (!enabledFound) {
    error = ConfigValidationError::NO_ENABLED_RADIOS;
    return false;
  }
  return true;
}

bool ConfigValidator::validate(const RuntimeConfig& config, ConfigValidationError& error,
                               uint8_t& radioIndex) {
  error = ConfigValidationError::NONE;
  radioIndex = 0;
  if (!terminated(config.wifiSsid) || !terminated(config.wifiPass) ||
      !terminated(config.staticIp) || !terminated(config.gatewayIp) ||
      !terminated(config.subnetMask) || !terminated(config.dns1Ip) ||
      !terminated(config.dns2Ip) || !terminated(config.timezone) || !terminated(config.ntpServer)) {
    error = ConfigValidationError::STRING_NOT_TERMINATED;
    return false;
  }
  if (config.wifiSsid[0] == '\0') {
    error = ConfigValidationError::SSID_MISSING;
    return false;
  }
  if (!config.useStaticIp) {
    // DHCP nie wymaga wartości w polach adresacji statycznej.
  } else if (!validIp(config.staticIp)) {
    error = ConfigValidationError::INVALID_STATIC_IP;
    return false;
  } else if (!validIp(config.gatewayIp)) {
    error = ConfigValidationError::INVALID_GATEWAY;
    return false;
  } else if (!validIp(config.subnetMask)) {
    error = ConfigValidationError::INVALID_SUBNET;
    return false;
  } else if (!validIp(config.dns1Ip)) {
    error = ConfigValidationError::INVALID_DNS1;
    return false;
  } else if (!validIp(config.dns2Ip)) {
    error = ConfigValidationError::INVALID_DNS2;
    return false;
  }

  if (config.timezone[0] == '\0') {
    error = ConfigValidationError::TIMEZONE_INVALID;
    return false;
  }
  if (config.ntpServer[0] == '\0') {
    error = ConfigValidationError::NTP_SERVER_INVALID;
    return false;
  }

  if (config.volumeScreenTimeoutMs < VOLUME_TIMEOUT_MIN_MS || config.volumeScreenTimeoutMs > VOLUME_TIMEOUT_MAX_MS) { error = ConfigValidationError::VOLUME_TIMEOUT_OUT_OF_RANGE; return false; }
  if (config.volumeRepeatMs < VOLUME_REPEAT_MIN_MS || config.volumeRepeatMs > VOLUME_REPEAT_MAX_MS) { error = ConfigValidationError::VOLUME_REPEAT_OUT_OF_RANGE; return false; }
  if (config.scrollStartDelayMs < SCROLL_DELAY_MIN_MS || config.scrollStartDelayMs > SCROLL_DELAY_MAX_MS) { error = ConfigValidationError::SCROLL_DELAY_OUT_OF_RANGE; return false; }
  if (config.scrollStepMs < SCROLL_STEP_MIN_MS || config.scrollStepMs > SCROLL_STEP_MAX_MS) { error = ConfigValidationError::SCROLL_STEP_OUT_OF_RANGE; return false; }
  if (config.radioMenuTimeoutMs < RADIO_MENU_TIMEOUT_MIN_MS || config.radioMenuTimeoutMs > RADIO_MENU_TIMEOUT_MAX_MS) { error = ConfigValidationError::RADIO_MENU_TIMEOUT_OUT_OF_RANGE; return false; }
  if (config.radioConnectingMinMs < RADIO_CONNECTING_MIN_MS || config.radioConnectingMinMs > RADIO_CONNECTING_MAX_MS) { error = ConfigValidationError::RADIO_CONNECTING_TIMEOUT_OUT_OF_RANGE; return false; }
  if (config.deepSleepTimeoutSec < DEEP_SLEEP_TIMEOUT_MIN_SEC || config.deepSleepTimeoutSec > DEEP_SLEEP_TIMEOUT_MAX_SEC) { error = ConfigValidationError::DEEP_SLEEP_TIMEOUT_OUT_OF_RANGE; return false; }
  if (config.stoppedTimeoutSec < STOPPED_TIMEOUT_MIN_SEC || config.stoppedTimeoutSec > STOPPED_TIMEOUT_MAX_SEC) { error = ConfigValidationError::STOPPED_TIMEOUT_OUT_OF_RANGE; return false; }

  return validateRadios(config, error, radioIndex);
}

bool ConfigValidator::validate(const RuntimeConfig& config, ValidationResult& result) {
  ConfigValidationError error = ConfigValidationError::NONE;
  uint8_t index = 0;
  result = {};
  if (validate(config, error, index)) return true;
  result.ok = false;
  result.error = error;
  result.index = index;
  switch (error) {
    case ConfigValidationError::SSID_MISSING:
    case ConfigValidationError::SSID_TOO_LONG:
      result.section = ValidationSection::NETWORK;
      result.field = ValidationField::SSID;
      break;
    case ConfigValidationError::INVALID_STATIC_IP: result.section = ValidationSection::NETWORK; result.field = ValidationField::STATIC_IP; break;
    case ConfigValidationError::INVALID_GATEWAY: result.section = ValidationSection::NETWORK; result.field = ValidationField::GATEWAY; break;
    case ConfigValidationError::INVALID_SUBNET: result.section = ValidationSection::NETWORK; result.field = ValidationField::SUBNET; break;
    case ConfigValidationError::INVALID_DNS1: result.section = ValidationSection::NETWORK; result.field = ValidationField::DNS1; break;
    case ConfigValidationError::INVALID_DNS2: result.section = ValidationSection::NETWORK; result.field = ValidationField::DNS2; break;
    case ConfigValidationError::TIMEZONE_INVALID: result.section = ValidationSection::REMOTE; result.field = ValidationField::TIMEZONE; break;
    case ConfigValidationError::NTP_SERVER_INVALID: result.section = ValidationSection::REMOTE; result.field = ValidationField::NTP_SERVER; break;
    case ConfigValidationError::NO_ENABLED_RADIOS:
      result.section = ValidationSection::RADIO;
      break;
    case ConfigValidationError::RADIO_ID_INVALID: case ConfigValidationError::RADIO_ID_DUPLICATE: result.section = ValidationSection::RADIO; result.field = ValidationField::RADIO_ID; break;
    case ConfigValidationError::RADIO_NAME_MISSING: result.section = ValidationSection::RADIO; result.field = ValidationField::RADIO_NAME; break;
    case ConfigValidationError::RADIO_HOST_MISSING: result.section = ValidationSection::RADIO; result.field = ValidationField::RADIO_HOST; break;
    case ConfigValidationError::RADIO_PORT_OUT_OF_RANGE: result.section = ValidationSection::RADIO; result.field = ValidationField::RADIO_PORT; break;
    case ConfigValidationError::RADIO_PATH_INVALID: result.section = ValidationSection::RADIO; result.field = ValidationField::RADIO_PATH; break;
    default: result.section = ValidationSection::REMOTE; break;
  }
  return false;
}