#include "BacklightController.h"

#include "HardwareConfig.h"

bool BacklightController::begin(uint8_t initialBrightnessPercent) {
  if (!ledcAttach(PIN_LCD_BACKLIGHT, LCD_BACKLIGHT_PWM_HZ,
                  LCD_BACKLIGHT_PWM_RESOLUTION_BITS)) {
    return false;
  }
  ready_ = true;
  return setBrightnessPercent(initialBrightnessPercent);
}

bool BacklightController::setBrightnessPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  brightnessPercent_ = percent;
  restoredBrightnessPercent_ = percent;
  if (!ready_) return false;
  return ledcWrite(PIN_LCD_BACKLIGHT, dutyForPercent(percent));
}

void BacklightController::off() {
  if (ready_) ledcWrite(PIN_LCD_BACKLIGHT, 0);
}

void BacklightController::restore() {
  if (ready_) ledcWrite(PIN_LCD_BACKLIGHT, dutyForPercent(restoredBrightnessPercent_));
}

uint32_t BacklightController::dutyForPercent(uint8_t percent) const {
  const uint32_t maxDuty = (1UL << LCD_BACKLIGHT_PWM_RESOLUTION_BITS) - 1UL;
  return (maxDuty * percent) / 100UL;
}