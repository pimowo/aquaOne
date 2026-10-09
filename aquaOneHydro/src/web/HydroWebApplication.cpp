#include "HydroWebApplication.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "hydrosense/HydroSenseConfigValidator.h"

using AquaCore::Diagnostics::HealthState;

namespace
{

void copyText(char* output, size_t capacity, const char* input)
{
    if (output == nullptr || capacity == 0U) return;
    output[0] = '\0';
    if (input != nullptr) std::strncpy(output, input, capacity - 1U);
    output[capacity - 1U] = '\0';
}

HealthState worse(HealthState left, HealthState right)
{
    return static_cast<uint8_t>(left) >= static_cast<uint8_t>(right)
        ? left : right;
}

}

HydroCoreDiagnosticsSource::HydroCoreDiagnosticsSource(
    const AquaCore::SystemService& system,
    const AquaCore::Network::NetworkService& network,
    const HydroConfigPersistence& storage
) : system_(system), network_(network), storage_(storage)
{
}

bool HydroCoreDiagnosticsSource::read(
    AquaCore::Web::CoreDiagnosticsProjection& out
) const
{
    if (!system_.isReady()) return false;
    AquaCore::Diagnostics::DiagnosticsSnapshot value {};
    value.system.ready = true;
    value.system.identity = system_.deviceIdentity();
    copyText(value.system.aquaCoreVersion, sizeof(value.system.aquaCoreVersion),
             system_.aquaCoreVersion());
    value.system.uptimeMs = system_.uptimeMs();
    value.system.restartReason = system_.restartReason();
    value.systemHealth = HealthState::Ok;
    value.timeHealth = HealthState::Unknown;

    const AquaCore::Config::StorageStatus storage = storage_.status();
    value.storage.backendReady = storage.backendReady;
    value.storage.hasValidPayload = storage.hasValidPayload;
    value.storage.activeSlot = storage.activeSlot;
    value.storage.activeGeneration = storage.activeGeneration;
    value.storage.lastLoadResult = storage.lastLoadResult;
    value.storage.lastSaveResult = storage.lastSaveResult;
    value.storageHealth = !storage.backendReady ? HealthState::Error
        : (storage.hasValidPayload ? HealthState::Ok : HealthState::Warning);

    value.network.available = true;
    value.network.state = network_.state();
    value.network.staEnabled = network_.isStaEnabled();
    value.network.connected = network_.isConnected();
    value.network.ipAddress = network_.ipAddress();
    value.network.rssi = network_.rssi();
    value.network.reconnectCount = network_.reconnectCount();
    value.network.connectionUptimeMs = static_cast<uint32_t>(
        std::min<uint64_t>(network_.connectionUptimeMs(),
                           std::numeric_limits<uint32_t>::max()));
    value.network.apEnabled = network_.isApEnabled();
    value.network.apActive = network_.isApActive();
    value.network.apState = network_.accessPointState();
    value.network.apIpAddress = network_.accessPointIpAddress();
    switch (network_.state())
    {
        case AquaCore::Network::NetworkState::Disabled:
        case AquaCore::Network::NetworkState::Connected:
            value.networkHealth = HealthState::Ok;
            break;
        case AquaCore::Network::NetworkState::Idle:
        case AquaCore::Network::NetworkState::Connecting:
        case AquaCore::Network::NetworkState::Disconnected:
            value.networkHealth = HealthState::Warning;
            break;
        case AquaCore::Network::NetworkState::Error:
            value.networkHealth = HealthState::Error;
            break;
    }
    value.overallHealth = worse(value.systemHealth,
        worse(value.storageHealth, value.networkHealth));
    out.value = value;
    return true;
}

HydroWebApplication::HydroWebApplication(
    HydroSenseConfig& config,
    HydroConfigPersistence& storage,
    HydroTopupActions& topupController,
    HydroBuzzerActions& buzzerController,
    const SystemStatus& status,
    const AquaCore::SystemService& system,
    AquaCore::Web::PublishedSnapshot<SystemStatus>& statusTarget,
    AquaCore::Web::PublishedSnapshot<HydroSettingsProjection>& settingsTarget,
    AquaCore::Web::PublishedSnapshot<HydroDiagnosticsProjection>& diagnosticsTarget,
    HydroApplicationBridge& bridge
) : config_(config), storage_(storage), topupController_(topupController),
    buzzerController_(buzzerController), status_(status), system_(system),
    statusTarget_(statusTarget), settingsTarget_(settingsTarget),
    diagnosticsTarget_(diagnosticsTarget), bridge_(bridge)
{
}

bool HydroWebApplication::processOne()
{
    return bridge_.processOne(execute, this);
}

HydroWebResult HydroWebApplication::execute(
    const HydroWebRequest& request, void* context
)
{
    HydroWebApplication* self = static_cast<HydroWebApplication*>(context);
    if (self == nullptr) return HydroWebResult::ValidationFailure;
    switch (request.kind)
    {
        case HydroWebRequestKind::ToggleServiceMode:
            self->topupController_.setServiceMode(
                !self->topupController_.isServiceMode());
            return HydroWebResult::CompletedSuccess;
        case HydroWebRequestKind::MuteBuzzer:
            self->buzzerController_.mute();
            return HydroWebResult::CompletedSuccess;
        case HydroWebRequestKind::ResetLockout:
            self->topupController_.resetLockout();
            return HydroWebResult::CompletedSuccess;
        case HydroWebRequestKind::SaveSettings:
            return self->applySettings(request.settings);
    }
    return HydroWebResult::ValidationFailure;
}

HydroWebResult HydroWebApplication::applySettings(
    const HydroSettingsRequest& request
)
{
    HydroSenseConfig candidate = config_;
    candidate.floatActiveLow = request.floatActiveLow;
    candidate.floatUsePullup = request.floatUsePullup;
    candidate.floatDebounceMs = request.floatDebounceMs;
    candidate.ultrasonicMinDistanceCm = request.ultrasonicMinDistanceCm;
    candidate.ultrasonicMaxDistanceCm = request.ultrasonicMaxDistanceCm;
    candidate.ultrasonicTimeoutUs = request.ultrasonicTimeoutUs;
    candidate.tankEmptyDistanceCm = request.tankEmptyDistanceCm;
    candidate.tankFullDistanceCm = request.tankFullDistanceCm;
    candidate.tankSampleIntervalMs = request.tankSampleIntervalMs;
    candidate.tankMaxFailedSeries = request.tankMaxFailedSeries;
    candidate.reserveLowPercent = request.reserveLowPercent;
    candidate.reserveCriticalPercent = request.reserveCriticalPercent;
    candidate.reserveHysteresisPercent = request.reserveHysteresisPercent;
    candidate.topupStartDelayMs = request.topupStartDelayMs;
    candidate.topupMaxPumpRuntimeMs = request.topupMaxPumpRuntimeMs;
    candidate.wifiStaEnabled = request.wifiStaEnabled;
    copyText(candidate.wifiSsid, sizeof(candidate.wifiSsid), request.wifiSsid);
    copyText(candidate.wifiHostname, sizeof(candidate.wifiHostname), request.wifiHostname);
    candidate.wifiAutoReconnect = request.wifiAutoReconnect;
    candidate.wifiReconnectIntervalMs = request.wifiReconnectIntervalMs;
    candidate.wifiApEnabled = request.wifiApEnabled;
    copyText(candidate.wifiApSsid, sizeof(candidate.wifiApSsid), request.wifiApSsid);
    if (request.replaceWifiPassword)
        copyText(candidate.wifiPassword, sizeof(candidate.wifiPassword), request.wifiPassword);
    if (request.replaceApPassword)
        copyText(candidate.wifiApPassword, sizeof(candidate.wifiApPassword), request.wifiApPassword);

    if (!validateHydroSenseConfig(candidate))
        return HydroWebResult::ValidationFailure;
    if (!storage_.save(candidate)) return HydroWebResult::StorageFailure;
    config_ = candidate;
    restartRequested_ = true;
    return HydroWebResult::CompletedSuccess;
}

bool HydroWebApplication::publish()
{
    HydroSettingsProjection settings {};
    settings.floatActiveLow = config_.floatActiveLow;
    settings.floatUsePullup = config_.floatUsePullup;
    settings.floatDebounceMs = config_.floatDebounceMs;
    settings.ultrasonicMinDistanceCm = config_.ultrasonicMinDistanceCm;
    settings.ultrasonicMaxDistanceCm = config_.ultrasonicMaxDistanceCm;
    settings.ultrasonicTimeoutUs = config_.ultrasonicTimeoutUs;
    settings.tankEmptyDistanceCm = config_.tankEmptyDistanceCm;
    settings.tankFullDistanceCm = config_.tankFullDistanceCm;
    settings.tankSampleIntervalMs = config_.tankSampleIntervalMs;
    settings.tankMaxFailedSeries = config_.tankMaxFailedSeries;
    settings.reserveLowPercent = config_.reserveLowPercent;
    settings.reserveCriticalPercent = config_.reserveCriticalPercent;
    settings.reserveHysteresisPercent = config_.reserveHysteresisPercent;
    settings.topupStartDelayMs = config_.topupStartDelayMs;
    settings.topupMaxPumpRuntimeMs = config_.topupMaxPumpRuntimeMs;
    settings.wifiStaEnabled = config_.wifiStaEnabled;
    copyText(settings.wifiSsid, sizeof(settings.wifiSsid), config_.wifiSsid);
    copyText(settings.wifiHostname, sizeof(settings.wifiHostname), config_.wifiHostname);
    settings.wifiAutoReconnect = config_.wifiAutoReconnect;
    settings.wifiReconnectIntervalMs = config_.wifiReconnectIntervalMs;
    settings.wifiApEnabled = config_.wifiApEnabled;
    copyText(settings.wifiApSsid, sizeof(settings.wifiApSsid), config_.wifiApSsid);

    HydroDiagnosticsProjection diagnostics {};
    diagnostics.system.identity = system_.deviceIdentity();
    copyText(diagnostics.system.aquaCoreVersion,
             sizeof(diagnostics.system.aquaCoreVersion), system_.aquaCoreVersion());
    diagnostics.system.uptimeMs = system_.uptimeMs();
    diagnostics.system.restartReason = system_.restartReason();
    diagnostics.system.ready = system_.isReady();
    diagnostics.storage = storage_.status();
    diagnostics.status = status_;

    const bool statusPublished = statusTarget_.publish(status_);
    const bool settingsPublished = settingsTarget_.publish(settings);
    const bool diagnosticsPublished = diagnosticsTarget_.publish(diagnostics);
    return statusPublished && settingsPublished && diagnosticsPublished;
}

bool HydroWebApplication::restartRequested() const
{
    return restartRequested_;
}

void HydroWebApplication::clearRestartRequest()
{
    restartRequested_ = false;
}
