#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Time {

constexpr uint8_t NTP_MAX_SERVERS = 3U;
constexpr size_t NTP_SERVER_NAME_CAPACITY = 64U;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10'000U;
constexpr uint32_t NTP_SYNC_INTERVAL_MS =
    24UL * 60UL * 60UL * 1000UL;

} // namespace Time
} // namespace AquaCore