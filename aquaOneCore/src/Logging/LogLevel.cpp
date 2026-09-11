#include "AquaCore/Logging/LogLevel.h"

namespace AquaCore {

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Off:
        default:
            return "OFF";
    }
}

} // namespace AquaCore