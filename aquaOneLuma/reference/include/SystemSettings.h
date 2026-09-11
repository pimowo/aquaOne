#pragma once

#include <Arduino.h>

struct SystemSettingsSnapshot {
  bool brightnessPresent{false};
  uint8_t brightness{0};
  bool serialDebugPresent{false};
  uint8_t serialDebug{0};
  bool usbSleepInhibitPresent{false};
  uint8_t usbSleepInhibit{0};
};

struct SystemSettings {
  uint8_t lcdBrightnessPercent{80};
  bool serialDebug{false};
  bool usbSleepInhibit{true};
};

class SystemSettingsStore {
public:
  void load(SystemSettings& settings) const;
  bool saveBrightness(uint8_t brightnessPercent) const;
  bool saveSerialDebug(bool enabled) const;
  bool saveUsbSleepInhibit(bool enabled) const;
  bool captureSnapshot(SystemSettingsSnapshot& snapshot) const;
  bool restoreSnapshot(const SystemSettingsSnapshot& snapshot) const;
};