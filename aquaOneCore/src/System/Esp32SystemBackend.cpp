#include "AquaCore/System/SystemService.h"

#include <Arduino.h>
#include <esp_system.h>

#include "System/Esp32RestartReason.h"

namespace AquaCore {
namespace {

class Esp32SystemBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override {
        return millis();
    }

    RestartReason restartReason() const override {
        return Detail::mapEsp32RestartReason(
            static_cast<int32_t>(esp_reset_reason())
        );
    }
};

SystemBackend& defaultBackend() {
    static Esp32SystemBackend backend;
    return backend;
}

} // namespace

SystemService::SystemService()
    : SystemService(defaultBackend()) {
}

} // namespace AquaCore
