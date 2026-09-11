#include "SystemSettings.h"

#include <Preferences.h>

#include "ConfigDefaults.h"
#include "ConfigValidator.h"

namespace {
constexpr char NVS_NAMESPACE[] = "yosys";
constexpr char BRIGHTNESS_KEY[] = "brightness";
constexpr char SERIAL_DEBUG_KEY[] = "serial_debug";
constexpr char USB_SLEEP_INHIBIT_KEY[] = "usb_sleep";
}

// Ustawienia systemowe są w yosys poza ConfigPayloadV1, aby nie zmieniać jego schematu.
void SystemSettingsStore::load(SystemSettings& settings) const {
  settings.lcdBrightnessPercent = ConfigDefaults::kLcdBrightnessPercent;
  settings.serialDebug = false;
  settings.usbSleepInhibit = true;

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true)) return;
  const uint8_t brightness = preferences.getUChar(BRIGHTNESS_KEY,
      ConfigDefaults::kLcdBrightnessPercent);
  const uint8_t serialDebug = preferences.getUChar(SERIAL_DEBUG_KEY, 0);
  const uint8_t usbSleepInhibit = preferences.getUChar(USB_SLEEP_INHIBIT_KEY, 1);
  preferences.end();

  if (ConfigValidator::isValidLcdBrightness(brightness)) {
    settings.lcdBrightnessPercent = brightness;
  }
  settings.serialDebug = serialDebug != 0;
  settings.usbSleepInhibit = usbSleepInhibit != 0;
}

bool SystemSettingsStore::captureSnapshot(SystemSettingsSnapshot& snapshot) const {
  snapshot = {};
  Preferences preferences;
  // Otwarcie do odczytu/zapisu działa także przy pierwszym zapisie bez namespace yosys.
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;

  snapshot.brightnessPresent = preferences.isKey(BRIGHTNESS_KEY);
  if (snapshot.brightnessPresent) snapshot.brightness = preferences.getUChar(BRIGHTNESS_KEY);
  snapshot.serialDebugPresent = preferences.isKey(SERIAL_DEBUG_KEY);
  if (snapshot.serialDebugPresent) snapshot.serialDebug = preferences.getUChar(SERIAL_DEBUG_KEY);
  snapshot.usbSleepInhibitPresent = preferences.isKey(USB_SLEEP_INHIBIT_KEY);
  if (snapshot.usbSleepInhibitPresent) {
    snapshot.usbSleepInhibit = preferences.getUChar(USB_SLEEP_INHIBIT_KEY);
  }
  preferences.end();
  return true;
}

bool SystemSettingsStore::restoreSnapshot(const SystemSettingsSnapshot& snapshot) const {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;

  const bool brightnessRestored = snapshot.brightnessPresent
      ? preferences.putUChar(BRIGHTNESS_KEY, snapshot.brightness) == 1
      : (!preferences.isKey(BRIGHTNESS_KEY) || preferences.remove(BRIGHTNESS_KEY));
  const bool serialDebugRestored = snapshot.serialDebugPresent
      ? preferences.putUChar(SERIAL_DEBUG_KEY, snapshot.serialDebug) == 1
      : (!preferences.isKey(SERIAL_DEBUG_KEY) || preferences.remove(SERIAL_DEBUG_KEY));
  const bool usbSleepRestored = snapshot.usbSleepInhibitPresent
      ? preferences.putUChar(USB_SLEEP_INHIBIT_KEY, snapshot.usbSleepInhibit) == 1
      : (!preferences.isKey(USB_SLEEP_INHIBIT_KEY) || preferences.remove(USB_SLEEP_INHIBIT_KEY));
  preferences.end();
  return brightnessRestored && serialDebugRestored && usbSleepRestored;
}
bool SystemSettingsStore::saveBrightness(uint8_t brightnessPercent) const {
  if (!ConfigValidator::isValidLcdBrightness(brightnessPercent)) return false;
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = preferences.putUChar(BRIGHTNESS_KEY, brightnessPercent) == 1;
  preferences.end();
  return saved;
}

bool SystemSettingsStore::saveSerialDebug(bool enabled) const {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = preferences.putUChar(SERIAL_DEBUG_KEY, enabled ? 1 : 0) == 1;
  preferences.end();
  return saved;
}
bool SystemSettingsStore::saveUsbSleepInhibit(bool enabled) const {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = preferences.putUChar(USB_SLEEP_INHIBIT_KEY, enabled ? 1 : 0) == 1;
  preferences.end();
  return saved;
}
