#pragma once

#include <Arduino.h>
#include "DeveloperConfig.h"
#include "SerialDiagnostics.h"

#if defined(DEBUG_UART) && DEBUG_UART
#define DEBUG_LOGF(...) do { if (SerialDiagnostics::enabled()) Serial.printf(__VA_ARGS__); } while (false)
#define DEBUG_LOGLN(...) do { if (SerialDiagnostics::enabled()) Serial.println(__VA_ARGS__); } while (false)
#define DEBUG_LOGWRITE(buffer, length) do { if (SerialDiagnostics::enabled()) Serial.write((buffer), (length)); } while (false)
#else
#define DEBUG_LOGF(...) do { } while (false)
#define DEBUG_LOGLN(...) do { } while (false)
#define DEBUG_LOGWRITE(buffer, length) do { } while (false)
#endif