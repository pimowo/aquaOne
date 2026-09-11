#include "AquaCore/System/RestartReason.h"

namespace AquaCore {

const char* restartReasonName(RestartReason reason) {
    switch (reason) {
        case RestartReason::PowerOn:
            return "POWER_ON";
        case RestartReason::Software:
            return "SOFTWARE";
        case RestartReason::Watchdog:
            return "WATCHDOG";
        case RestartReason::Brownout:
            return "BROWNOUT";
        case RestartReason::DeepSleep:
            return "DEEP_SLEEP";
        case RestartReason::Panic:
            return "PANIC";
        case RestartReason::Other:
            return "OTHER";
        case RestartReason::Unknown:
        default:
            return "UNKNOWN";
    }
}

} // namespace AquaCore