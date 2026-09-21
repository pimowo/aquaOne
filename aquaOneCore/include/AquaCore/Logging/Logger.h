#pragma once

#include "AquaCore/Logging/LoggingConfig.h"
#include "AquaCore/Logging/LogSink.h"
#include "AquaCore/Logging/LogWriter.h"

namespace AquaCore {

class Logger : public LogWriter {
public:
    Logger();
    explicit Logger(LogSink& sink);

    void setSink(LogSink& sink);
    void setLevel(LogLevel level);
    LogLevel level() const;

    void log(
        LogLevel level,
        const char* module,
        const char* message
    );

    void write(
        LogLevel level,
        const char* module,
        const char* message
    ) override;

    void debug(const char* module, const char* message);
    void info(const char* module, const char* message);
    void warning(const char* module, const char* message);
    void error(const char* module, const char* message);

    static constexpr bool compiledIn() {
        return AQUA_CORE_LOGGING_ENABLED != 0;
    }

private:
    bool shouldWrite(LogLevel messageLevel) const;

    LogSink* sink_;
    LogLevel minimumLevel_;
};

} // namespace AquaCore