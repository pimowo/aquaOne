#pragma once

class SerialDiagnostics {
public:
  static void begin(bool enabled);
  static void setEnabled(bool enabled);
  static bool enabled();

private:
  static bool enabled_;
};