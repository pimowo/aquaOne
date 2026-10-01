#pragma once

#include "AquaCore/System/RuntimeIdentity.h"

namespace AquaCore {
namespace Identity {

// For ESP32 CORE_INIT before ADC, I2S, Wi-Fi or Bluetooth initialization.
// The composition callback checks that this exclusive entropy window is open.
class Esp32RuntimeIdentityGenerator final : public RuntimeIdentityGenerator {
public:
    using EntropyWindowAvailable = bool (*)(void* context);

    Esp32RuntimeIdentityGenerator(
        EntropyWindowAvailable entropyWindowAvailable,
        void* context
    ) : entropyWindowAvailable_(entropyWindowAvailable), context_(context) {}

    bool generate(RuntimeIdentity& output) override;

private:
    EntropyWindowAvailable entropyWindowAvailable_;
    void* context_;
};

} // namespace Identity
} // namespace AquaCore
