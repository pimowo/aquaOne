#include "AquaCore/Network/NetworkService.h"

namespace AquaCore {
namespace Network {
namespace {

bool isNullTerminated(
    const char* value,
    size_t capacity
) {
    if (value == nullptr || capacity == 0U) {
        return false;
    }

    for (size_t index = 0U; index < capacity; ++index) {
        if (value[index] == '\0') {
            return true;
        }
    }

    return false;
}

bool isEmpty(const char* value) {
    return value == nullptr || value[0] == '\0';
}

} // namespace

NetworkService::NetworkService(NetworkBackend& backend)
    : backend_(backend) {
}

bool NetworkService::begin(const NetworkConfig& config) {
    if (initialized_) {
        if (config_.staEnabled) {
            backend_.disconnectSta();
        }
        if (config_.apEnabled) {
            backend_.stopAccessPoint();
        }
    }

    initialized_ = false;
    config_ = {};
    state_ = NetworkState::Disabled;
    apState_ = AccessPointState::Disabled;
    apIpAddress_ = {};
    reconnectCount_ = 0U;
    reconnectAnchorMs_ = 0U;
    hasReconnectAnchor_ = false;
    clearStaRuntime();

    if (!validateConfig(config)) {
        state_ = NetworkState::Error;
        return false;
    }

    config_ = config;
    initialized_ = true;

    if (!config_.staEnabled && !config_.apEnabled) {
        return true;
    }

    bool success = true;

    if (config_.apEnabled) {
        if (backend_.startAccessPoint(
            config_.apSsid,
            config_.apPassword
        )) {
            apState_ = AccessPointState::Active;
            apIpAddress_ = backend_.accessPointIp();
        } else {
            apState_ = AccessPointState::Error;
            success = false;
        }
    }

    if (!config_.staEnabled) {
        state_ = success
            ? NetworkState::Idle
            : NetworkState::Error;
        return success;
    }

    if (
        !isEmpty(config_.hostname) &&
        !backend_.setHostname(config_.hostname)
    ) {
        state_ = NetworkState::Error;
        return false;
    }

    if (!backend_.beginSta(
        config_.ssid,
        config_.password
    )) {
        state_ = NetworkState::Error;
        return false;
    }

    state_ = NetworkState::Connecting;
    return success;
}

void NetworkService::update(uint32_t nowMs) {
    if (!initialized_ || !config_.staEnabled) {
        return;
    }

    const BackendStaState backendState =
        backend_.staState();

    if (backendState == BackendStaState::Connected) {
        if (state_ != NetworkState::Connected) {
            connectedSinceMs_ = nowMs;
            hasConnectedSince_ = true;
        }

        state_ = NetworkState::Connected;
        hasReconnectAnchor_ = false;
        ipAddress_ = backend_.localIp();
        rssi_ = backend_.rssi();
        return;
    }

    if (backendState == BackendStaState::Connecting) {
        state_ = NetworkState::Connecting;
        clearStaRuntime();
        return;
    }

    observeDisconnected(backendState, nowMs);
}

NetworkState NetworkService::state() const {
    return state_;
}

AccessPointState NetworkService::accessPointState() const {
    return apState_;
}

bool NetworkService::isConnected() const {
    return state_ == NetworkState::Connected;
}

bool NetworkService::isStaEnabled() const {
    return initialized_ && config_.staEnabled;
}

bool NetworkService::isApEnabled() const {
    return initialized_ && config_.apEnabled;
}

bool NetworkService::isApActive() const {
    return apState_ == AccessPointState::Active;
}

const char* NetworkService::ssid() const {
    return isStaEnabled() ? config_.ssid : "";
}

const char* NetworkService::hostname() const {
    return initialized_ ? config_.hostname : "";
}

const char* NetworkService::accessPointSsid() const {
    return isApEnabled() ? config_.apSsid : "";
}

int32_t NetworkService::rssi() const {
    return isConnected() ? rssi_ : 0;
}

IpAddress NetworkService::ipAddress() const {
    return isConnected() ? ipAddress_ : IpAddress {};
}

IpAddress NetworkService::accessPointIpAddress() const {
    return isApActive() ? apIpAddress_ : IpAddress {};
}

uint32_t NetworkService::reconnectCount() const {
    return reconnectCount_;
}

uint32_t NetworkService::connectionUptimeMs(
    uint32_t nowMs
) const {
    if (!isConnected() || !hasConnectedSince_) {
        return 0U;
    }

    return nowMs - connectedSinceMs_;
}

bool NetworkService::validateConfig(
    const NetworkConfig& config
) {
    if (
        !isNullTerminated(config.ssid, sizeof(config.ssid)) ||
        !isNullTerminated(
            config.password,
            sizeof(config.password)
        ) ||
        !isNullTerminated(
            config.hostname,
            sizeof(config.hostname)
        ) ||
        !isNullTerminated(
            config.apSsid,
            sizeof(config.apSsid)
        ) ||
        !isNullTerminated(
            config.apPassword,
            sizeof(config.apPassword)
        )
    ) {
        return false;
    }

    if (
        config.staEnabled &&
        (
            isEmpty(config.ssid) ||
            (
                config.autoReconnect &&
                config.reconnectIntervalMs == 0U
            )
        )
    ) {
        return false;
    }

    return !config.apEnabled || !isEmpty(config.apSsid);
}

void NetworkService::clearStaRuntime() {
    ipAddress_ = {};
    rssi_ = 0;
    connectedSinceMs_ = 0U;
    hasConnectedSince_ = false;
}

void NetworkService::observeDisconnected(
    BackendStaState backendState,
    uint32_t nowMs
) {
    clearStaRuntime();

    state_ = backendState == BackendStaState::Error
        ? NetworkState::Error
        : NetworkState::Disconnected;

    if (!hasReconnectAnchor_) {
        reconnectAnchorMs_ = nowMs;
        hasReconnectAnchor_ = true;
        return;
    }

    if (
        !config_.autoReconnect ||
        nowMs - reconnectAnchorMs_ <
            config_.reconnectIntervalMs
    ) {
        return;
    }

    reconnectAnchorMs_ = nowMs;
    ++reconnectCount_;

    if (backend_.reconnectSta()) {
        state_ = NetworkState::Connecting;
    } else {
        state_ = NetworkState::Error;
    }
}

} // namespace Network
} // namespace AquaCore
