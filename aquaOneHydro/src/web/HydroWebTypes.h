#pragma once

#include <stdint.h>
#include <type_traits>

#include <AquaCore/Config/StorageService.h>
#include <AquaCore/Web/CoreWebProjections.h>
#include <AquaCore/Web/WebApplicationBridge.h>

#include "hydrosense/HydroSenseConfig.h"
#include "hydrosense/SystemStatus.h"

struct HydroSettingsProjection
{
    bool floatActiveLow = false;
    bool floatUsePullup = false;
    uint32_t floatDebounceMs = 0;
    float ultrasonicMinDistanceCm = 0.0f;
    float ultrasonicMaxDistanceCm = 0.0f;
    uint32_t ultrasonicTimeoutUs = 0;
    float tankEmptyDistanceCm = 0.0f;
    float tankFullDistanceCm = 0.0f;
    uint32_t tankSampleIntervalMs = 0;
    uint8_t tankMaxFailedSeries = 0;
    float reserveLowPercent = 0.0f;
    float reserveCriticalPercent = 0.0f;
    float reserveHysteresisPercent = 0.0f;
    uint32_t topupStartDelayMs = 0;
    uint32_t topupMaxPumpRuntimeMs = 0;
    bool wifiStaEnabled = false;
    char wifiSsid[33] {};
    char wifiHostname[33] {};
    bool wifiAutoReconnect = false;
    uint32_t wifiReconnectIntervalMs = 0;
    bool wifiApEnabled = false;
    char wifiApSsid[33] {};
};

struct HydroSettingsRequest
{
    bool floatActiveLow = false;
    bool floatUsePullup = false;
    uint32_t floatDebounceMs = 0;
    float ultrasonicMinDistanceCm = 0.0f;
    float ultrasonicMaxDistanceCm = 0.0f;
    uint32_t ultrasonicTimeoutUs = 0;
    float tankEmptyDistanceCm = 0.0f;
    float tankFullDistanceCm = 0.0f;
    uint32_t tankSampleIntervalMs = 0;
    uint8_t tankMaxFailedSeries = 0;
    float reserveLowPercent = 0.0f;
    float reserveCriticalPercent = 0.0f;
    float reserveHysteresisPercent = 0.0f;
    uint32_t topupStartDelayMs = 0;
    uint32_t topupMaxPumpRuntimeMs = 0;
    bool wifiStaEnabled = false;
    char wifiSsid[33] {};
    char wifiPassword[65] {};
    bool replaceWifiPassword = false;
    char wifiHostname[33] {};
    bool wifiAutoReconnect = false;
    uint32_t wifiReconnectIntervalMs = 0;
    bool wifiApEnabled = false;
    char wifiApSsid[33] {};
    char wifiApPassword[65] {};
    bool replaceApPassword = false;
};

enum class HydroWebRequestKind : uint8_t
{
    ToggleServiceMode,
    MuteBuzzer,
    ResetLockout,
    SaveSettings
};

struct HydroWebRequest
{
    HydroWebRequestKind kind = HydroWebRequestKind::ToggleServiceMode;
    HydroSettingsRequest settings {};
};

enum class HydroWebResult : uint8_t
{
    CompletedSuccess,
    ValidationFailure,
    StorageFailure
};

struct HydroDiagnosticsProjection
{
    AquaCore::Web::CoreSystemProjection system {};
    AquaCore::Config::StorageStatus storage {};
    SystemStatus status {};
};

using HydroApplicationBridge = AquaCore::Web::WebApplicationBridge<
    HydroWebRequest,
    HydroWebResult,
    4U
>;

static_assert(std::is_trivially_copyable<HydroSettingsProjection>::value,
              "Hydro settings projection must remain copied");
static_assert(std::is_trivially_copyable<HydroSettingsRequest>::value,
              "Hydro settings request must remain copied");
static_assert(std::is_trivially_copyable<HydroWebRequest>::value,
              "Hydro Web request must remain copied");
static_assert(std::is_trivially_copyable<HydroDiagnosticsProjection>::value,
              "Hydro diagnostics projection must remain copied");
