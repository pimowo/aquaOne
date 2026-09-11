#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include "BacklightController.h"
#include "BatteryMonitor.h"
#include "BootModeDetector.h"
#include "ClockService.h"
#include "ConfigAccessPoint.h"
#include "ConfigPortal.h"
#include "ConfigStore.h"
#include "ConfigModeScreen.h"
#include "DisplayDriver.h"
#include "DeveloperConfig.h"
#include "FactoryDefaults.h"
#include "InputManager.h"

#include "UIRenderer.h"
#include "WiFiMgr.h"
#include "WSClient.h"
#include "DataStore.h"
#include "RadioManager.h"
#include "RuntimeConfig.h"
#include "SystemSettings.h"
#include "SerialDiagnostics.h"
#include "SleepManager.h"
#include "PowerStatusMonitor.h"
#include "FirmwareVersion.h"

#include "DebugLog.h"
#if defined(DEBUG_UART) && DEBUG_UART
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#if defined(DEBUG_UART) && DEBUG_UART
namespace {
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}
namespace {
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 60000;
uint32_t lastHeapLogAt = 0;

void logHeapDiagnostics() {
  const uint32_t now = millis();
  if (now - lastHeapLogAt < HEAP_LOG_INTERVAL_MS) return;
  lastHeapLogAt = now;

  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minFreeHeap = ESP.getMinFreeHeap();
  const uint32_t largestBlock =
      static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  const uint32_t fragmentationPercent =
      freeHeap == 0 || largestBlock >= freeHeap
          ? 0
          : ((freeHeap - largestBlock) * 100UL) / freeHeap;
  DEBUG_LOGF("[HEAP][%lu] free=%lu min=%lu largest=%lu frag=%lu%%\n", now,
             freeHeap, minFreeHeap, largestBlock, fragmentationPercent);
}

void logStackWatermark(const char* point) {
  const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
  DEBUG_LOGF("[STACK] %s free=%lu B\n", point,
             static_cast<unsigned long>(words * sizeof(StackType_t)));
}
}
}
#endif
SystemSettings systemSettings;
SystemSettingsStore systemSettingsStore;
BacklightController backlight;
PowerStatusMonitor powerStatus;
RuntimeConfig runtimeConfig;
ConfigStore configStore;
DisplayDriver display;
ConfigModeScreen configModeScreen(display);
ConfigAccessPoint configAccessPoint;
ConfigPortal* configPortal = nullptr;
WiFiMgr wifi(runtimeConfig);
DataStore ds;
WSClient ws(wifi, ds);
RadioManager radios(ws, ds, runtimeConfig);
ClockService clockService(runtimeConfig);
BatteryMonitor battery;
UIRenderer ui(display, wifi, ws, ds, radios, clockService, battery, powerStatus, runtimeConfig);
InputManager input(ws, radios, runtimeConfig);
SleepManager sleepManager(backlight, powerStatus, input, ws, wifi, ds, runtimeConfig, systemSettings);
bool configModeActive = false;
bool configDisplayActive = false;
uint32_t lastConfigClientCheckAt = 0;
void enterConfigMode(bool displayReady) {
  configModeActive = true;
  if (!backlight.isReady()) {
    backlight.begin(systemSettings.lcdBrightnessPercent);
  }
  backlight.setBrightnessPercent(systemSettings.lcdBrightnessPercent);
  DEBUG_LOGF("[CONFIG_MODE] display ready=%u brightness=%u\n", displayReady ? 1U : 0U,
             static_cast<unsigned>(systemSettings.lcdBrightnessPercent));

  if (displayReady) {
    configModeScreen.show();
    configDisplayActive = true;
    DEBUG_LOGLN("[CONFIG_MODE] screen rendered");
  }
  configAccessPoint.begin();
  if (configAccessPoint.isRunning()) {
    static ConfigPortal configPortalInstance(configAccessPoint, configStore, systemSettings,
                                             systemSettingsStore, backlight);
    configPortal = &configPortalInstance;
    configPortal->begin(runtimeConfig);
    DEBUG_LOGLN("[CONFIG_MODE] AP ready");
  }
}

void setup() {
  Serial.begin(115200);
  systemSettingsStore.load(systemSettings);
  SerialDiagnostics::begin(systemSettings.serialDebug);
#if defined(DEBUG_UART) && DEBUG_UART
  DEBUG_LOGF("[BOOT] reset_reason=%s\n", resetReasonName(esp_reset_reason()));
#endif
#if defined(DEBUG_UART) && DEBUG_UART
  logStackWatermark("setup start");
#endif
  DEBUG_LOGF("[BOOT][%lu] BOOT_START firmware=%s\n", millis(), FirmwareVersion::kString);
  backlight.begin(systemSettings.lcdBrightnessPercent);

  const bool displayReady = display.begin();
  BootModeDetector bootModeDetector;
  const bool manualConfigRequested = bootModeDetector.detect() == BootMode::CONFIG;

  FactoryDefaults::load(runtimeConfig);
  const bool hasOperationalConfig = configStore.load(runtimeConfig);
#if defined(DEBUG_UART) && DEBUG_UART
  logStackWatermark("after ConfigStore load");
#endif

  if (manualConfigRequested || !hasOperationalConfig) {
#if defined(DEBUG_UART) && DEBUG_UART
    logStackWatermark("Config Mode entry");
#endif
    enterConfigMode(displayReady);
    return;
  }
  powerStatus.begin();
  battery.begin();
  ds.begin();
  ws.begin();
  radios.begin();
  input.begin();
  if (displayReady) {
    ui.begin();
  }
  wifi.begin();
}

void loop() {
#if defined(DEBUG_UART) && DEBUG_UART
  if (SerialDiagnostics::enabled()) logHeapDiagnostics();
#endif
  if (configModeActive) {
    const uint32_t now = millis();
    if (configDisplayActive && now - lastConfigClientCheckAt >= 500) {
      lastConfigClientCheckAt = now;
      configModeScreen.updateClientStatus(WiFi.softAPgetStationNum() > 0);
    }
    if (configPortal) configPortal->loop();
    return;
  }
  wifi.loop();
  clockService.loop(wifi);
  ws.loop();
  radios.loop();
  powerStatus.loop();
  battery.loop();
  input.setContext(ui.isRadioSelectActive() ? InputManager::InputContext::RADIO_SELECT
                                             : ui.isVolumeScreenActive() ? InputManager::InputContext::VOLUME
                                                                        : InputManager::InputContext::MAIN);
  input.loop();
  switch (input.consumeUiAction()) {
    case InputManager::UiAction::OPEN_RADIO_SELECT:
      ui.openRadioSelect();
      break;
    case InputManager::UiAction::RADIO_SELECT_UP:
      ui.moveRadioSelection(-1);
      break;
    case InputManager::UiAction::RADIO_SELECT_DOWN:
      ui.moveRadioSelection(1);
      break;
    case InputManager::UiAction::RADIO_SELECT_CONFIRM:
      ui.confirmRadioSelection();
      break;
    case InputManager::UiAction::NONE:
      break;
  }
  const int8_t selectedRadio = ui.consumeRadioSelection();
  if (selectedRadio >= 0) radios.selectRadio(static_cast<uint8_t>(selectedRadio));
  if (input.consumeVolumeScreenRequest()) ui.showVolumeScreen();
  ui.loop();
  sleepManager.loop();
}
