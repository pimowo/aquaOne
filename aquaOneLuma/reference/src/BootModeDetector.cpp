#include "BootModeDetector.h"

#include "HardwareConfig.h"

#include "DebugLog.h"
BootMode BootModeDetector::detect() {
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  // GPIO8 jest pinem strapping ESP32-C3; ROM odczytuje go przed startem firmware;
  // detektor ocenia gest aplikacyjny dopiero po uruchomieniu programu.
  if (digitalRead(BUTTON_UP_PIN) != LOW || digitalRead(BUTTON_DOWN_PIN) != LOW) {
    DEBUG_LOGF("[BOOT][%lu] BOOT_MODE_NORMAL\n", millis());
    return BootMode::NORMAL;
  }

  const uint32_t startedAt = millis();
  DEBUG_LOGF("[BOOT][%lu] CONFIG_GESTURE_START\n", startedAt);
  while (millis() - startedAt < kConfirmMs) {
    if (digitalRead(BUTTON_UP_PIN) != LOW || digitalRead(BUTTON_DOWN_PIN) != LOW) {
      DEBUG_LOGF("[BOOT][%lu] CONFIG_GESTURE_CANCELLED\n", millis());
      DEBUG_LOGF("[BOOT][%lu] BOOT_MODE_NORMAL\n", millis());
      return BootMode::NORMAL;
    }
    yield();
  }

  DEBUG_LOGF("[BOOT][%lu] BOOT_MODE_CONFIG\n", millis());
  return BootMode::CONFIG;
}
