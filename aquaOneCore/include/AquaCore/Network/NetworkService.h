#pragma once

#include <stdint.h>

#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkTypes.h"

namespace AquaCore {
namespace Network {

class NetworkService {
public:
    explicit NetworkService(NetworkBackend& backend);

    bool begin(const NetworkConfig& config);
    void update(uint32_t nowMs);

    NetworkState state() const;
    AccessPointState accessPointState() const;

    bool isConnected() const;
    bool isStaEnabled() const;
    bool isApEnabled() const;
    bool isApActive() const;

    const char* ssid() const;
    const char* hostname() const;
    const char* accessPointSsid() const;

    int32_t rssi() const;
    IpAddress ipAddress() const;
    IpAddress accessPointIpAddress() const;

    uint32_t reconnectCount() const;
    uint32_t connectionUptimeMs(uint32_t nowMs) const;

    static bool validateConfig(const NetworkConfig& config);

private:
    NetworkBackend& backend_;
    NetworkConfig config_ {};
    NetworkState state_ = NetworkState::Disabled;
    AccessPointState apState_ = AccessPointState::Disabled;
    IpAddress ipAddress_ {};
    IpAddress apIpAddress_ {};
    int32_t rssi_ = 0;
    uint32_t reconnectCount_ = 0U;
    uint32_t reconnectAnchorMs_ = 0U;
    uint32_t connectedSinceMs_ = 0U;
    bool initialized_ = false;
    bool hasReconnectAnchor_ = false;
    bool hasConnectedSince_ = false;

    void clearStaRuntime();
    void observeDisconnected(
        BackendStaState backendState,
        uint32_t nowMs
    );
};

} // namespace Network
} // namespace AquaCore
