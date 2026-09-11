#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Network {

struct IpAddress {
    uint8_t octets[4] {};

    bool isSet() const {
        return
            octets[0] != 0U ||
            octets[1] != 0U ||
            octets[2] != 0U ||
            octets[3] != 0U;
    }
};

enum class NetworkState : uint8_t {
    Disabled = 0U,
    Idle,
    Connecting,
    Connected,
    Disconnected,
    Error
};

enum class AccessPointState : uint8_t {
    Disabled = 0U,
    Active,
    Error
};

enum class BackendStaState : uint8_t {
    Disconnected = 0U,
    Connecting,
    Connected,
    Error
};

} // namespace Network
} // namespace AquaCore
