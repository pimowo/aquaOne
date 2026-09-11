#pragma once

#include <Arduino.h>

class BacklightController {
public:
  bool begin(uint8_t initialBrightnessPercent);
  bool setBrightnessPercent(uint8_t percent);
  uint8_t brightnessPercent() const { return brightnessPercent_; }
  void off();
  void restore();
  bool isReady() const { return ready_; }

private:
  uint32_t dutyForPercent(uint8_t percent) const;

  uint8_t brightnessPercent_{0};
  uint8_t restoredBrightnessPercent_{0};
  bool ready_{false};
};