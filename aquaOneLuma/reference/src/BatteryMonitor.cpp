#include "BatteryMonitor.h"

#include "HardwareConfig.h"

#include <math.h>

#include "DebugLog.h"
void BatteryMonitor::begin() {
  (void)analogReadMilliVolts(BATTERY_ADC_PIN);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  lastMeasurementAt_ = millis() - kUpdateIntervalMs;
}

void BatteryMonitor::loop() {
  const uint32_t now = millis();
  if (now - lastMeasurementAt_ < kUpdateIntervalMs) return;
  lastMeasurementAt_ = now;

  uint32_t sampleTotalMv = 0;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    sampleTotalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  const float pinMv = static_cast<float>(sampleTotalMv) / kSamples;
  const uint16_t batteryMv = static_cast<uint16_t>(lroundf(pinMv * BATTERY_DIVIDER_RATIO));

  uint8_t batteryPercent = 0;
  if (BATTERY_MAX_MV > BATTERY_MIN_MV) {
    if (batteryMv >= BATTERY_MAX_MV) batteryPercent = 100;
    else if (batteryMv > BATTERY_MIN_MV) {
      batteryPercent = static_cast<uint8_t>(
          ((static_cast<uint32_t>(batteryMv - BATTERY_MIN_MV) * 100UL) /
           static_cast<uint32_t>(BATTERY_MAX_MV - BATTERY_MIN_MV)));
    }
  }

  millivolts_ = batteryMv;
  percent_ = batteryPercent;
  hasReading_ = true;

  DEBUG_LOGF("[BAT][%lu] adc=%.0f battery=%u percent=%u\n", static_cast<unsigned long>(now),
                static_cast<double>(pinMv), static_cast<unsigned int>(batteryMv),
                static_cast<unsigned int>(batteryPercent));
}
