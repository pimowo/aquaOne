#pragma once

#include <stdint.h>

namespace AquaCore {

enum class RestartReason : uint8_t {
    Unknown,
    PowerOn,
    Software,
    Watchdog,
    Brownout,
    DeepSleep,
    Panic,
    Other
};

const char* restartReasonName(RestartReason reason);

} // namespace AquaCore