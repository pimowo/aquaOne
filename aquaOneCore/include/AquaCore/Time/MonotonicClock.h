#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Time {

// Borrowed runtime-local clock capability. The concrete owner must outlive
// every consumer; consumers never destroy it through this interface.
class MonotonicClock {
public:
    // Milliseconds have no calendar meaning and need not start at zero.
    // Later reads in the same runtime must be >= earlier reads.
    virtual uint64_t nowMilliseconds() const = 0;

protected:
    ~MonotonicClock() = default;
};

} // namespace Time
} // namespace AquaCore
