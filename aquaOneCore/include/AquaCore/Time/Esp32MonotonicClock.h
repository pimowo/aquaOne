#pragma once

#include "AquaCore/Time/MonotonicClock.h"

namespace AquaCore {
namespace Time {

class Esp32MonotonicClock final : public MonotonicClock {
public:
    uint64_t nowMilliseconds() const override;
};

} // namespace Time
} // namespace AquaCore
