#pragma once

#include <Arduino.h>

class PowerStatusMonitor {
public:
  void begin();
  void loop();

  bool usbPresent() const { return usbPresent_; }
  bool charging() const { return charging_; }
  bool onBattery() const { return !usbPresent_; }
  bool externalPowerPresent() const { return usbPresent_; }

private:
  struct InputState {
    bool raw{false};
    bool stable{false};
    uint32_t changedAt{0};
  };

  void update(InputState& input, bool rawValue, uint32_t now);

  InputState usb_{};
  InputState charge_{};
  bool usbPresent_{false};
  bool charging_{false};
  uint32_t lastPollAt_{0};
};