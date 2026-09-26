#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Commands {

// Semantic command category, independent of transport, role, action and result.
enum class CommandClass : uint8_t {
    NormalDomain,
    SystemRecovery
};

} // namespace Commands
} // namespace AquaCore