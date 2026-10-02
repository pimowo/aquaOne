#pragma once

#include <stdint.h>

#include "AquaCore/Time/UtcTimestamp.h"

namespace AquaCore {
namespace Time {

enum class WallClockReadResult : uint8_t {
    Success,
    Unavailable
};

// Borrowed read-only UTC capability. Its concrete owner must outlive every
// consumer; consumers never destroy it through this interface.
class WallClock {
public:
    // Only Success makes out usable for this read. On Unavailable, callers
    // must ignore out; its contents need not be cleared or preserved.
    // Wall time can jump in either direction and is not for elapsed timing.
    virtual WallClockReadResult readUtc(UtcTimestamp& out) const = 0;

protected:
    ~WallClock() = default;
};

} // namespace Time
} // namespace AquaCore
