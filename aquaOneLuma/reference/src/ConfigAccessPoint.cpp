#include "ConfigAccessPoint.h"

#include <WiFi.h>

#include "DebugLog.h"
namespace {
constexpr char kSsid[] = "yoPILOT";
constexpr char kPassword[] = "12345987";
const IPAddress kLocalIp(192, 168, 4, 1);
const IPAddress kGateway(192, 168, 4, 1);
const IPAddress kSubnet(255, 255, 255, 0);
}

bool ConfigAccessPoint::begin() {
  running_ = false;
  DEBUG_LOGF("[CONFIG_AP][%lu] starting\n", millis());

  if (!WiFi.mode(WIFI_AP) || !WiFi.softAPConfig(kLocalIp, kGateway, kSubnet)) {
    Serial.printf("[CONFIG_AP][%lu] AP_CONFIG_FAILED\n", millis());
    return false;
  }

  if (!WiFi.softAP(kSsid, kPassword)) {
    Serial.printf("[CONFIG_AP][%lu] AP_START_FAILED\n", millis());
    return false;
  }

  ip_ = WiFi.softAPIP();
  running_ = true;
  DEBUG_LOGF("[CONFIG_AP][%lu] AP_READY ip=%u.%u.%u.%u\n", millis(), ip_[0], ip_[1], ip_[2], ip_[3]);
  return true;
}
