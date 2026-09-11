#pragma once

#include <stdint.h>

#include "AquaCore/System/RestartReason.h"

namespace AquaCore {
namespace Detail {

// Internal ESP32 adapter. The public Aqua Core API never exposes
// esp_reset_reason_t or any other ESP-IDF type.
RestartReason mapEsp32RestartReason(int32_t nativeReason);

} // namespace Detail
} // namespace AquaCore