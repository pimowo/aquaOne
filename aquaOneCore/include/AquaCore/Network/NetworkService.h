#pragma once

#include <stdint.h>

#include "AquaCore/Network/NetworkBackend.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkTypes.h"
#include "AquaCore/Time/MonotonicClock.h"

namespace AquaCore {
namespace Network {

class NetworkService {
public:
    explicit NetworkService(NetworkBackend& backend);
    NetworkService(NetworkBackend& backend, const Time::MonotonicClock& clock);
    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;
    NetworkService(NetworkService&&) = delete;
    NetworkService& operator=(NetworkService&&) = delete;

    bool begin(const NetworkConfig& config);
    // Use with the clock-injected constructor. The legacy constructor needs
    // update(nowMs) to feed its per-instance millis() extension.
    void update();
    // Compatibility adapter for existing millis() callers. With an injected
    // clock, nowMs is ignored; the borrowed clock remains authoritative.
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
    uint64_t connectionUptimeMs() const;
    // Compatibility view; with an injected clock, nowMs is ignored.
    uint32_t connectionUptimeMs(uint32_t nowMs) const;

    static bool validateConfig(const NetworkConfig& config);

private:
    class LegacyMillisClock final : public Time::MonotonicClock {
    public:
        uint64_t nowMilliseconds() const override { return nowMs_; }
        void observe(uint32_t nowMs);
        uint64_t expanded(uint32_t nowMs) const;

    private:
        uint64_t nowMs_ = 0U;
        uint32_t lastMs_ = 0U;
        bool hasObservation_ = false;
    };

    NetworkBackend& backend_;
    LegacyMillisClock legacyClock_ {};
    const Time::MonotonicClock& clock_;
    bool legacyTiming_;
    NetworkConfig config_ {};
    NetworkState state_ = NetworkState::Disabled;
    AccessPointState apState_ = AccessPointState::Disabled;
    IpAddress ipAddress_ {};
    IpAddress apIpAddress_ {};
    int32_t rssi_ = 0;
    uint32_t reconnectCount_ = 0U;
    uint64_t reconnectAnchorMs_ = 0U;
    uint32_t activeReconnectIntervalMs_ = DEFAULT_RECONNECT_INTERVAL_MS;
    uint64_t connectingStartedMs_ = 0U;
    uint64_t connectedSinceMs_ = 0U;
    bool initialized_ = false;
    bool initializationFailed_ = false;
    bool hasReconnectAnchor_ = false;
    bool hasConnectingStarted_ = false;
    bool hasConnectedSince_ = false;

    void clearStaRuntime();
    void observeDisconnected(
        BackendStaState backendState,
        uint64_t nowMs
    );
    uint64_t connectionUptimeAt(uint64_t nowMs) const;
};

} // namespace Network
} // namespace AquaCore
