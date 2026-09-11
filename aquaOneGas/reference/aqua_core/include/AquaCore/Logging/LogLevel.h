#pragma once

#include <stdint.h>

namespace AquaCore {

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warning,
    Error,
    Off
};

const char* logLevelName(LogLevel level);

} // namespace AquaCore