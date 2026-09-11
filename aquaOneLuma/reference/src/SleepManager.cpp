#include "SleepManager.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "BacklightController.h"
#include "DataStore.h"
#include "DebugLog.h"
#include "InputManager.h"
#include "PowerStatusMonitor.h"
#include "RuntimeConfig.h"
#include "WSClient.h"
#include "WiFiMgr.h"
#include "SystemSettings.h"

SleepManager::SleepManager(BacklightController& backlight, PowerStatusMonitor& power,
                           InputManager& input, WSClient& ws, WiFiMgr& wifi, DataStore& dataStore,
                           const RuntimeConfig& config, const SystemSettings& systemSettings)
    : backlight_(backlight), power_(power), input_(input), ws_(ws), wifi_(wifi), dataStore_(dataStore),
      config_(config), systemSettings_(systemSettings) {}

void SleepManager::loop() {
  const bool usbPresent = power_.usbPresent();
  if (systemSettings_.usbSleepInhibit) {
    if (usbStateKnown_ && lastUsbPresent_ && !usbPresent) {
      input_.resetActivityTimer(millis());
      DEBUG_LOGLN("[POWER] USB disconnected -> sleep timer reset");
    }
    lastUsbPresent_ = usbPresent;
    usbStateKnown_ = true;
  } else {
    usbStateKnown_ = false;
  }

  if (enteringSleep_) return;
  const uint32_t timeoutSec = radioIsPlaying() ? config_.deepSleepTimeoutSec
                                                : config_.stoppedTimeoutSec;
  if (timeoutSec == 0) return;
  if (systemSettings_.usbSleepInhibit && usbPresent) return;
  const uint32_t timeoutMs = timeoutSec * 1000UL;
  if (static_cast<uint32_t>(millis() - input_.lastActivityMs()) < timeoutMs) return;
  enterDeepSleep();
}

bool SleepManager::radioIsPlaying() const {
  const String state = dataStore_.playerState();
  return state == "play" || state == "playing" || state == "radio";
}

void SleepManager::enterDeepSleep() {
  enteringSleep_ = true;
  DEBUG_LOGLN("[SLEEP] 1 begin");
  backlight_.off();
  DEBUG_LOGLN("[SLEEP] 2 backlight off");
  DEBUG_LOGLN("[SLEEP] 3 ws disconnect start");
  ws_.prepareForSleepShutdown();
  ws_.disconnect();
  DEBUG_LOGLN("[SLEEP] 4 ws disconnect done");
  DEBUG_LOGLN("[SLEEP] 5 wifi off start");
  wifi_.prepareForSleepShutdown();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  DEBUG_LOGLN("[SLEEP] 6 wifi off done");
  // Celowo nie ma źródła wybudzenia ext0/ext1/GPIO/timer: jedyną drogą jest RST/EN.
  DEBUG_LOGLN("[SLEEP] 7 calling esp_deep_sleep_start");
  esp_deep_sleep_start();
}