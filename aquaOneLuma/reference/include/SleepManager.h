#pragma once

#include <Arduino.h>

class BacklightController;
class DataStore;
class InputManager;
class PowerStatusMonitor;
class WSClient;
class WiFiMgr;
struct SystemSettings;
struct RuntimeConfig;

class SleepManager {
public:
  SleepManager(BacklightController& backlight, PowerStatusMonitor& power,
               InputManager& input, WSClient& ws, WiFiMgr& wifi, DataStore& dataStore,
               const RuntimeConfig& config, const SystemSettings& systemSettings);
  void loop();

private:
  bool radioIsPlaying() const;
  void enterDeepSleep();

  BacklightController& backlight_;
  PowerStatusMonitor& power_;
  InputManager& input_;
  WSClient& ws_;
  WiFiMgr& wifi_;
  DataStore& dataStore_;
  const RuntimeConfig& config_;
  const SystemSettings& systemSettings_;
  bool enteringSleep_{false};
  bool usbStateKnown_{false};
  bool lastUsbPresent_{false};
};