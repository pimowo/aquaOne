#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/Config/StorageService.h"
#include "AquaCore/Network/NetworkConfig.h"
#include "AquaCore/Network/NetworkTypes.h"
#include "AquaCore/System/DeviceIdentity.h"
#include "AquaCore/System/RestartReason.h"

namespace AquaCore {
namespace Diagnostics {

constexpr size_t VERSION_TEXT_CAPACITY = 16U;
constexpr size_t TIME_PROVIDER_NAME_CAPACITY = 32U;

enum class HealthState : uint8_t {
    Ok = 0U,
    Unknown,
    Warning,
    Error
};

enum class TimeState : uint8_t {
    Unavailable = 0U,
    Invalid,
    Valid
};

enum class NtpSyncResult : uint8_t {
    NotAvailable = 0U,
    NotAttempted,
    Success,
    Failure
};

struct SystemDiagnostics {
    bool ready = false;
    DeviceIdentity identity {};
    char aquaCoreVersion[VERSION_TEXT_CAPACITY] {};
    uint32_t uptimeMs = 0U;
    RestartReason restartReason = RestartReason::Unknown;
};

struct TimeDiagnostics {
    bool rtcReady = false;
    bool rtcValid = false;
    bool ntpAvailable = false;
    bool ntpInitialized = false;
    bool ntpSyncInProgress = false;
    NtpSyncResult lastSyncResult =
        NtpSyncResult::NotAvailable;
    bool hasLastSuccessfulSyncAge = false;
    uint32_t lastSuccessfulSyncAgeMs = 0U;
    TimeState state = TimeState::Unavailable;
    char providerName[TIME_PROVIDER_NAME_CAPACITY] {};
};

struct StorageDiagnostics {
    bool backendReady = false;
    bool hasValidPayload = false;
    Config::StorageSlot activeSlot =
        Config::StorageSlot::None;
    uint32_t activeGeneration = 0U;
    Config::StorageOperationResult lastLoadResult =
        Config::StorageOperationResult::NotAttempted;
    Config::StorageOperationResult lastSaveResult =
        Config::StorageOperationResult::NotAttempted;
};

struct NetworkDiagnostics {
    bool available = false;
    Network::NetworkState state =
        Network::NetworkState::Disabled;
    bool staEnabled = false;
    bool connected = false;
    char ssid[Network::WIFI_SSID_CAPACITY] {};
    char hostname[Network::WIFI_HOSTNAME_CAPACITY] {};
    Network::IpAddress ipAddress {};
    int32_t rssi = 0;
    uint32_t reconnectCount = 0U;
    uint32_t connectionUptimeMs = 0U;
    bool apEnabled = false;
    bool apActive = false;
    Network::AccessPointState apState =
        Network::AccessPointState::Disabled;
    char apSsid[Network::WIFI_SSID_CAPACITY] {};
    Network::IpAddress apIpAddress {};
};

struct DiagnosticsSnapshot {
    SystemDiagnostics system {};
    TimeDiagnostics time {};
    StorageDiagnostics storage {};
    NetworkDiagnostics network {};
    HealthState systemHealth = HealthState::Unknown;
    HealthState timeHealth = HealthState::Unknown;
    HealthState storageHealth = HealthState::Unknown;
    HealthState networkHealth = HealthState::Unknown;
    HealthState overallHealth = HealthState::Unknown;
};

} // namespace Diagnostics
} // namespace AquaCore
