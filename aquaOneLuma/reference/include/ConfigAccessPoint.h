#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class ConfigAccessPoint {
public:
  bool begin();

  bool isRunning() const { return running_; }
  IPAddress ip() const { return ip_; }

private:
  bool running_ = false;
  IPAddress ip_ = IPAddress(192, 168, 4, 1);
};
