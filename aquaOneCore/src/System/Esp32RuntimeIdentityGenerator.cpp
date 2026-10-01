#include "AquaCore/System/Esp32RuntimeIdentityGenerator.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <bootloader_random.h>
#include <esp_random.h>

namespace AquaCore {
namespace Identity {

bool Esp32RuntimeIdentityGenerator::generate(RuntimeIdentity& output) {
    if (entropyWindowAvailable_ == nullptr ||
        !entropyWindowAvailable_(context_)) {
        return false;
    }

    // Enable the non-RF entropy source only during this bounded operation.
    // Caller must guarantee ADC/I2S/RF are not in use during this window.
    bootloader_random_enable();
    bool generated = false;
    for (unsigned attempt = 0U; attempt < 4U; ++attempt) {
        const uint64_t candidate =
            (static_cast<uint64_t>(esp_random()) << 32U) | esp_random();
        if (RuntimeIdentity::fromValue(candidate, output)) {
            generated = true;
            break;
        }
    }
    bootloader_random_disable();
    return generated;
}

} // namespace Identity
} // namespace AquaCore
#endif
