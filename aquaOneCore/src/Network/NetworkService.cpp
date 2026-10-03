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

void NetworkService::LegacyMillisClock::observe(uint32_t nowMs) {
    if (!hasObservation_) {
        nowMs_ = nowMs;
        hasObservation_ = true;
    } else {
        nowMs_ += static_cast<uint32_t>(nowMs - lastMs_);
    }
    lastMs_ = nowMs;
}

uint64_t NetworkService::LegacyMillisClock::expanded(uint32_t nowMs) const {
    return hasObservation_
        ? nowMs_ + static_cast<uint32_t>(nowMs - lastMs_)
        : static_cast<uint64_t>(nowMs);
}

NetworkService::NetworkService(NetworkBackend& backend)
    : backend_(backend), clock_(legacyClock_), legacyTiming_(true) {
}

NetworkService::NetworkService(
    NetworkBackend& backend,
    const Time::MonotonicClock& clock
) : backend_(backend), clock_(clock), legacyTiming_(false) {
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
    initializationFailed_ = false;
    config_ = {};
    state_ = NetworkState::Disabled;
    apState_ = AccessPointState::Disabled;
    apIpAddress_ = {};
    reconnectCount_ = 0U;
    reconnectAnchorMs_ = 0U;
    activeReconnectIntervalMs_ = DEFAULT_RECONNECT_INTERVAL_MS;
    connectingStartedMs_ = 0U;
    hasReconnectAnchor_ = false;
    hasConnectingStarted_ = false;
    clearStaRuntime();

    if (!validateConfig(config)) {
        state_ = NetworkState::Error;
        return false;
    }

    config_ = config;
    activeReconnectIntervalMs_ = config_.reconnectIntervalMs;
    if (!config_.staEnabled && !config_.apEnabled) {
        initialized_ = true;
        return true;
    }

    if (!backend_.applyRadioPolicy(
        config_.persistent,
        config_.sdkAutoReconnect,
        config_.powerSave
    )) {
        state_ = NetworkState::Error;
        return false;
    }
    initialized_ = true;

    if (config_.apEnabled) {
        if (backend_.startAccessPoint(
            config_.apSsid,
            config_.apPassword
        )) {
            apState_ = AccessPointState::Active;
            apIpAddress_ = backend_.accessPointIp();
        } else {
            apState_ = AccessPointState::Error;
            state_ = NetworkState::Error;
            initializationFailed_ = true;
            return false;
        }
    }

    if (!config_.staEnabled) {
        state_ = NetworkState::Idle;
        return true;
    }

    if (
        !isEmpty(config_.hostname) &&
        !backend_.setHostname(config_.hostname)
    ) {
        state_ = NetworkState::Error;
        initializationFailed_ = true;
        return false;
    }

    if (!backend_.beginSta(
        config_.ssid,
        config_.password
    )) {
        state_ = NetworkState::Error;
        initializationFailed_ = true;
        return false;
    }

    state_ = NetworkState::Connecting;
    return true;
}

void NetworkService::update(uint32_t nowMs) {
    if (legacyTiming_) {
        legacyClock_.observe(nowMs);
    }
    update();
}

void NetworkService::update() {
    if (!initialized_ || initializationFailed_ || !config_.staEnabled) {
        return;
    }

    const uint64_t nowMs = clock_.nowMilliseconds();

    const BackendStaState backendState =
        backend_.staState();

    if (backendState == BackendStaState::Connected) {
        if (state_ != NetworkState::Connected) {
            connectedSinceMs_ = nowMs;
            hasConnectedSince_ = true;
        }

        state_ = NetworkState::Connected;
        hasReconnectAnchor_ = false;
        hasConnectingStarted_ = false;
        connectingStartedMs_ = 0U;
        ipAddress_ = backend_.localIp();
        rssi_ = backend_.rssi();
        return;
    }

    if (backendState == BackendStaState::Connecting) {
        state_ = NetworkState::Connecting;
        clearStaRuntime();

        if (!hasConnectingStarted_) {
            connectingStartedMs_ = nowMs;
            hasConnectingStarted_ = true;
        } else if (
            config_.connectTimeoutMs > 0U &&
            (nowMs - connectingStartedMs_ >= config_.connectTimeoutMs)
        ) {
            observeDisconnected(BackendStaState::Disconnected, nowMs);
            return;
        }

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
    return static_cast<uint32_t>(
        connectionUptimeAt(legacyTiming_
            ? legacyClock_.expanded(nowMs)
            : clock_.nowMilliseconds())
    );
}

uint64_t NetworkService::connectionUptimeMs() const {
    return connectionUptimeAt(clock_.nowMilliseconds());
}

uint64_t NetworkService::connectionUptimeAt(uint64_t nowMs) const {
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
    uint64_t nowMs
) {
    clearStaRuntime();
    connectingStartedMs_ = 0U;
    hasConnectingStarted_ = false;

    state_ = backendState == BackendStaState::Error
        ? NetworkState::Error
        : NetworkState::Disconnected;

    const NetworkDisconnectReason reason =
        backend_.consumeDisconnectReason();

    if (!hasReconnectAnchor_) {
        reconnectAnchorMs_ = nowMs;
        hasReconnectAnchor_ = true;

        const bool isFastCandidate = (
            reason == NetworkDisconnectReason::AssociationExpired ||
            reason == NetworkDisconnectReason::ConnectionFailed ||
            reason == NetworkDisconnectReason::AssociationComebackTooLong
        );

        if (
            config_.fastRetryEnabled &&
            isFastCandidate &&
            config_.fastReconnectIntervalMs > 0U
        ) {
            activeReconnectIntervalMs_ = config_.fastReconnectIntervalMs;
        } else {
            activeReconnectIntervalMs_ = config_.reconnectIntervalMs;
        }

        return;
    }

    if (
        !config_.autoReconnect ||
        nowMs - reconnectAnchorMs_ < activeReconnectIntervalMs_
    ) {
        return;
    }

    reconnectAnchorMs_ = nowMs;
    hasReconnectAnchor_ = false;
    ++reconnectCount_;

    if (backend_.reconnectSta()) {
        state_ = NetworkState::Connecting;
        connectingStartedMs_ = nowMs;
        hasConnectingStarted_ = true;
    } else {
        state_ = NetworkState::Error;
    }
}

} // namespace Network
} // namespace AquaCore
