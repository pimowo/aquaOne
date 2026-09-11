#include "System/Esp32RestartReason.h"

#include <esp_system.h>

namespace AquaCore {
namespace Detail {

RestartReason mapEsp32RestartReason(int32_t nativeReason) {
    switch (nativeReason) {
        case ESP_RST_POWERON:
            return RestartReason::PowerOn;

        case ESP_RST_SW:
            return RestartReason::Software;

        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return RestartReason::Watchdog;

        case ESP_RST_BROWNOUT:
            return RestartReason::Brownout;

        case ESP_RST_DEEPSLEEP:
            return RestartReason::DeepSleep;

        case ESP_RST_PANIC:
            return RestartReason::Panic;

        case ESP_RST_UNKNOWN:
            return RestartReason::Unknown;

        default:
            return RestartReason::Other;
    }
}

} // namespace Detail
} // namespace AquaCore