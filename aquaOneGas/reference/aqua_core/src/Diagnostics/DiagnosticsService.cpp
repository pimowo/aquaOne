#include "AquaCore/Diagnostics/DiagnosticsService.h"

#include <cstring>

namespace AquaCore {
namespace Diagnostics {
namespace {

void copyText(
    char* destination,
    size_t capacity,
    const char* source
) {
    if (destination == nullptr || capacity == 0U) {
        return;
    }

    destination[0] = '\0';

    if (source == nullptr) {
        return;
    }

    std::strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

uint8_t severity(HealthState state) {
    switch (state) {
        case HealthState::Ok:
            return 0U;
        case HealthState::Unknown:
            return 1U;
        case HealthState::Warning:
            return 2U;
        case HealthState::Error:
            return 3U;
    }

    return 1U;
}

} // namespace

DiagnosticsService::DiagnosticsService(
    const SystemService& system,
    const Time::RtcService& rtc,
    const Config::StorageService& storage,
    const char* timeProviderName,
    const Time::NtpService* ntp,
    const Network::NetworkService* network
)
    : system_(system),
      rtc_(rtc),
      storage_(storage),
      ntp_(ntp),
      network_(network) {
    copyText(
        timeProviderName_,
        sizeof(timeProviderName_),
        timeProviderName
    );
}

DiagnosticsSnapshot DiagnosticsService::snapshot() const {
    DiagnosticsSnapshot result {};

    result.system.ready = system_.isReady();
    result.system.identity = system_.deviceIdentity();
    copyText(
        result.system.aquaCoreVersion,
        sizeof(result.system.aquaCoreVersion),
        system_.aquaCoreVersion()
    );
    result.system.uptimeMs = system_.uptimeMs();
    result.system.restartReason = system_.restartReason();

    result.time.rtcReady = rtc_.isInitialized();
    result.time.rtcValid = rtc_.isValid();
    result.time.state =
        !result.time.rtcReady
            ? TimeState::Unavailable
            : (
                result.time.rtcValid
                    ? TimeState::Valid
                    : TimeState::Invalid
            );
    copyText(
        result.time.providerName,
        sizeof(result.time.providerName),
        timeProviderName_
    );

    if (ntp_ != nullptr) {
        result.time.ntpAvailable = true;
        result.time.ntpInitialized = ntp_->isInitialized();
        result.time.ntpSyncInProgress =
            ntp_->isSyncInProgress();

        if (!ntp_->hasSyncResult()) {
            result.time.lastSyncResult =
                NtpSyncResult::NotAttempted;
        } else {
            result.time.lastSyncResult =
                ntp_->lastSyncSucceeded()
                    ? NtpSyncResult::Success
                    : NtpSyncResult::Failure;
        }

        result.time.hasLastSuccessfulSyncAge =
            ntp_->lastSuccessfulSyncAgeMs(
                result.system.uptimeMs,
                result.time.lastSuccessfulSyncAgeMs
            );
    }

    const Config::StorageStatus storageStatus =
        storage_.status();
    result.storage.backendReady =
        storageStatus.backendReady;
    result.storage.hasValidPayload =
        storageStatus.hasValidPayload;
    result.storage.activeSlot =
        storageStatus.activeSlot;
    result.storage.activeGeneration =
        storageStatus.activeGeneration;
    result.storage.lastLoadResult =
        storageStatus.lastLoadResult;
    result.storage.lastSaveResult =
        storageStatus.lastSaveResult;

    if (network_ != nullptr) {
        result.network.available = true;
        result.network.state = network_->state();
        result.network.staEnabled =
            network_->isStaEnabled();
        result.network.connected =
            network_->isConnected();
        copyText(
            result.network.ssid,
            sizeof(result.network.ssid),
            network_->ssid()
        );
        copyText(
            result.network.hostname,
            sizeof(result.network.hostname),
            network_->hostname()
        );
        result.network.ipAddress =
            network_->ipAddress();
        result.network.rssi = network_->rssi();
        result.network.reconnectCount =
            network_->reconnectCount();
        result.network.connectionUptimeMs =
            network_->connectionUptimeMs(
                result.system.uptimeMs
            );
        result.network.apEnabled =
            network_->isApEnabled();
        result.network.apActive =
            network_->isApActive();
        result.network.apState =
            network_->accessPointState();
        copyText(
            result.network.apSsid,
            sizeof(result.network.apSsid),
            network_->accessPointSsid()
        );
        result.network.apIpAddress =
            network_->accessPointIpAddress();
    }

    result.systemHealth = systemHealth(result.system);
    result.timeHealth = timeHealth(result.time);
    result.storageHealth = storageHealth(result.storage);
    result.networkHealth = networkHealth(result.network);
    result.overallHealth = worse(
        worse(result.systemHealth, result.timeHealth),
        result.storageHealth
    );

    if (result.network.available) {
        result.overallHealth = worse(
            result.overallHealth,
            result.networkHealth
        );
    }

    return result;
}

HealthState DiagnosticsService::systemHealth(
    const SystemDiagnostics& diagnostics
) {
    return diagnostics.ready
        ? HealthState::Ok
        : HealthState::Error;
}

HealthState DiagnosticsService::timeHealth(
    const TimeDiagnostics& diagnostics
) {
    if (!diagnostics.rtcReady) {
        return HealthState::Error;
    }

    if (!diagnostics.rtcValid) {
        if (
            diagnostics.ntpAvailable &&
            diagnostics.lastSyncResult ==
                NtpSyncResult::Failure
        ) {
            return HealthState::Error;
        }

        return HealthState::Warning;
    }

    if (
        diagnostics.ntpAvailable &&
        diagnostics.lastSyncResult ==
            NtpSyncResult::Failure
    ) {
        return HealthState::Warning;
    }

    return HealthState::Ok;
}

HealthState DiagnosticsService::storageHealth(
    const StorageDiagnostics& diagnostics
) {
    if (!diagnostics.backendReady) {
        return HealthState::Error;
    }

    if (
        diagnostics.lastSaveResult ==
            Config::StorageOperationResult::Failure
    ) {
        return HealthState::Error;
    }

    if (
        diagnostics.hasValidPayload &&
        diagnostics.lastLoadResult ==
            Config::StorageOperationResult::Failure
    ) {
        return HealthState::Warning;
    }

    return diagnostics.hasValidPayload
        ? HealthState::Ok
        : HealthState::Warning;
}

HealthState DiagnosticsService::networkHealth(
    const NetworkDiagnostics& diagnostics
) {
    if (!diagnostics.available) {
        return HealthState::Unknown;
    }

    if (
        diagnostics.state == Network::NetworkState::Disabled ||
        diagnostics.connected ||
        (
            !diagnostics.staEnabled &&
            diagnostics.apActive
        )
    ) {
        return HealthState::Ok;
    }

    if (
        diagnostics.state == Network::NetworkState::Disconnected ||
        diagnostics.state == Network::NetworkState::Error
    ) {
        return HealthState::Warning;
    }

    return HealthState::Unknown;
}

HealthState DiagnosticsService::worse(
    HealthState first,
    HealthState second
) {
    return severity(first) >= severity(second)
        ? first
        : second;
}

} // namespace Diagnostics
} // namespace AquaCore
