#pragma once

#include <Arduino.h>

class BatteryMonitor {
public:
  void begin();
  void loop();

  bool hasReading() const { return hasReading_; }
  uint16_t millivolts() const { return millivolts_; }
  uint8_t percent() const { return percent_; }

private:
  static constexpr uint8_t kSamples = 16;
  static constexpr uint32_t kUpdateIntervalMs = 5000;

  uint32_t lastMeasurementAt_{0};
  uint16_t millivolts_{0};
  uint8_t percent_{0};
  bool hasReading_{false};
};
