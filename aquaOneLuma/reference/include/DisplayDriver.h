#pragma once

#include <Arduino.h>

class DisplayDriver {
public:
  bool begin();

  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color);
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void pushPixels(const uint16_t* pixels, uint32_t count);
  void pushRect(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t* pixels);

private:
  void sendCommand(uint8_t command);
  void sendData(const uint8_t* data, size_t length);
  void sendData8(uint8_t value);
  void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h);
  void setRotation(uint8_t rotation);
  void reset();
  void initializeController();

  void* spi_{nullptr};
  bool initialized_{false};
  uint8_t transferBuffer_[4096]{};
};
