#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Time {

// Nonnegative UTC seconds since the Unix epoch. The number alone does not
// encode availability: zero is a valid timestamp when a read succeeds.
// Adapters converting a signed epoch value must validate it before conversion.
struct UtcTimestamp {
    uint64_t secondsSinceUnixEpoch = 0U;
};

} // namespace Time
} // namespace AquaCore
