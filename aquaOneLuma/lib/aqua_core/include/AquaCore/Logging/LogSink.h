#pragma once

#include "AquaCore/Logging/LogLevel.h"

namespace AquaCore {

class LogSink {
public:
    virtual ~LogSink() = default;

    virtual void write(
        LogLevel level,
        const char* module,
        const char* message
    ) = 0;
};

} // namespace AquaCore