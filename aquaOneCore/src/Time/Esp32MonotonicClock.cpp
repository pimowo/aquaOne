#include "AquaCore/Time/Esp32MonotonicClock.h"

#include <esp_timer.h>

namespace AquaCore {
namespace Time {

uint64_t Esp32MonotonicClock::nowMilliseconds() const {
    const int64_t elapsedMicroseconds = esp_timer_get_time();
    if (elapsedMicroseconds <= 0) {
        return 0U;
    }

    return static_cast<uint64_t>(elapsedMicroseconds) / 1000ULL;
}

} // namespace Time
} // namespace AquaCore
