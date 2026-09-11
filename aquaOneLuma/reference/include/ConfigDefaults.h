#pragma once

#include <Arduino.h>


// Wspólne domyślne wartości fabryczne używane przez FactoryDefaults i portal.
namespace ConfigDefaults {
constexpr bool kUseStaticIp = false;
constexpr uint16_t kWebSocketPort = 80;
constexpr char kWebSocketPath[] = "/ws";
constexpr uint8_t kLcdBrightnessPercent = 80;
constexpr uint32_t kVolumeScreenTimeoutMs = 3000;
constexpr uint32_t kVolumeRepeatMs = 250;
constexpr uint32_t kScrollStartDelayMs = 2000;
constexpr uint32_t kScrollStepMs = 30;
constexpr uint32_t kRadioMenuTimeoutMs = 5000;
constexpr uint32_t kRadioConnectingMinMs = 1500;
constexpr uint32_t kDeepSleepTimeoutSec = 120;
constexpr uint32_t kStoppedTimeoutSec = 30;
constexpr char kTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char kNtpServer[] = "pool.ntp.org";
}  // namespace ConfigDefaults