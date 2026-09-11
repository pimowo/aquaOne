#include "AquaCore/Logging/Logger.h"

#include <stdint.h>

namespace AquaCore {
namespace {

const char* safeText(const char* value) {
    return value == nullptr ? "" : value;
}

} // namespace

Logger::Logger()
    : sink_(nullptr),
      minimumLevel_(LogLevel::Info) {
}

Logger::Logger(LogSink& sink)
    : sink_(&sink),
      minimumLevel_(LogLevel::Info) {
}

void Logger::setSink(LogSink& sink) {
    sink_ = &sink;
}

void Logger::setLevel(LogLevel level) {
    minimumLevel_ = level;
}

LogLevel Logger::level() const {
    return minimumLevel_;
}

bool Logger::shouldWrite(LogLevel messageLevel) const {
#if AQUA_CORE_LOGGING_ENABLED
    if (
        sink_ == nullptr ||
        minimumLevel_ == LogLevel::Off ||
        messageLevel == LogLevel::Off
    ) {
        return false;
    }

    return
        static_cast<uint8_t>(messageLevel) >=
        static_cast<uint8_t>(minimumLevel_);
#else
    (void)messageLevel;
    return false;
#endif
}

void Logger::log(
    LogLevel messageLevel,
    const char* module,
    const char* message
) {
#if AQUA_CORE_LOGGING_ENABLED
    if (!shouldWrite(messageLevel)) {
        return;
    }

    sink_->write(
        messageLevel,
        safeText(module),
        safeText(message)
    );
#else
    (void)messageLevel;
    (void)module;
    (void)message;
#endif
}

void Logger::debug(
    const char* module,
    const char* message
) {
    log(LogLevel::Debug, module, message);
}

void Logger::info(
    const char* module,
    const char* message
) {
    log(LogLevel::Info, module, message);
}

void Logger::warning(
    const char* module,
    const char* message
) {
    log(LogLevel::Warning, module, message);
}

void Logger::error(
    const char* module,
    const char* message
) {
    log(LogLevel::Error, module, message);
}

} // namespace AquaCore