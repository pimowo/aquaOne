#pragma once

#include <Arduino.h>

enum class BootMode : uint8_t { NORMAL, CONFIG };

class BootModeDetector {
public:
  BootMode detect();

private:
  static constexpr uint32_t kConfirmMs = 1000;
};
