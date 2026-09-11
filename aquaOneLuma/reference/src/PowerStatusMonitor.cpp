#include "PowerStatusMonitor.h"

#include "DebugLog.h"
#include "HardwareConfig.h"

namespace {
bool isActive(uint8_t pin, uint8_t activeLevel) {
  return digitalRead(pin) == activeLevel;
}
}

void PowerStatusMonitor::begin() {
  pinMode(PIN_USB_PRESENT, USB_PRESENT_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
  pinMode(PIN_CHARGE_STATUS, CHARGE_STATUS_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);

  const uint32_t now = millis();
  usb_.raw = usb_.stable = isActive(PIN_USB_PRESENT, USB_PRESENT_ACTIVE_LEVEL);
  charge_.raw = charge_.stable = isActive(PIN_CHARGE_STATUS, CHARGE_STATUS_ACTIVE_LEVEL);
  usb_.changedAt = charge_.changedAt = now;
  usbPresent_ = usb_.stable;
  charging_ = charge_.stable;
  lastPollAt_ = now;
}

void PowerStatusMonitor::loop() {
  const uint32_t now = millis();
  if (now - lastPollAt_ < POWER_STATUS_POLL_MS) return;
  lastPollAt_ = now;

  const bool previousUsb = usbPresent_;
  const bool previousCharging = charging_;
  update(usb_, isActive(PIN_USB_PRESENT, USB_PRESENT_ACTIVE_LEVEL), now);
  update(charge_, isActive(PIN_CHARGE_STATUS, CHARGE_STATUS_ACTIVE_LEVEL), now);
  usbPresent_ = usb_.stable;
  charging_ = charge_.stable;

  if (previousUsb != usbPresent_ || previousCharging != charging_) {
    DEBUG_LOGF("[POWER][%lu] usb=%u charging=%u\n", now,
               static_cast<unsigned>(usbPresent_), static_cast<unsigned>(charging_));
  }
}

void PowerStatusMonitor::update(InputState& input, bool rawValue, uint32_t now) {
  if (rawValue != input.raw) {
    input.raw = rawValue;
    input.changedAt = now;
  }
  if (input.stable != input.raw && now - input.changedAt >= POWER_STATUS_DEBOUNCE_MS) {
    input.stable = input.raw;
  }
}