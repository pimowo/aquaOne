#pragma once

#include <Print.h>

#include "AquaCore/Logging/LogSink.h"

namespace AquaCore {

class SerialLogSink final : public LogSink {
public:
    explicit SerialLogSink(Print& output);

    void write(
        LogLevel level,
        const char* module,
        const char* message
    ) override;

private:
    Print& output_;
};

} // namespace AquaCore