#include "AquaCore/Logging/SerialLogSink.h"

namespace AquaCore {
namespace {

const char* safeText(const char* value) {
    return value == nullptr ? "" : value;
}

} // namespace

SerialLogSink::SerialLogSink(Print& output)
    : output_(output) {
}

void SerialLogSink::write(
    LogLevel level,
    const char* module,
    const char* message
) {
    output_.print('[');
    output_.print(logLevelName(level));
    output_.print("][");
    output_.print(safeText(module));
    output_.print("] ");
    output_.println(safeText(message));
}

} // namespace AquaCore