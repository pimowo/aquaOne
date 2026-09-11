#pragma once

#include <Arduino.h>

class DisplayDriver;

class ConfigModeScreen {
public:
  explicit ConfigModeScreen(DisplayDriver& display) : display_(display) {}
  void show();
  void updateClientStatus(bool connected);

private:
  void drawText(int16_t x, int16_t y, const String& text, uint8_t scale, uint16_t color);
  void drawCentered(int16_t y, const String& text, uint8_t scale, uint16_t color);
  DisplayDriver& display_;
  bool clientStatusKnown_{false};
  bool clientConnected_{false};
};
