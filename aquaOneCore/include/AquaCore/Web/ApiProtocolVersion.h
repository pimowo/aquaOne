#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Web {

struct ApiProtocolVersion {
    uint16_t major;
    uint16_t minor;
};

// Independent of firmware, Core and future HA integration release versions.
constexpr ApiProtocolVersion CURRENT_API_PROTOCOL_VERSION {1U, 0U};

} // namespace Web
} // namespace AquaCore
