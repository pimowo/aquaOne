#include "DoserDiagnosticsProjection.h"

#include <string.h>

using namespace AquaCore;

namespace {
Diagnostics::HealthState worse(Diagnostics::HealthState a,
                               Diagnostics::HealthState b) {
    return static_cast<uint8_t>(a) > static_cast<uint8_t>(b) ? a : b;
}
}

bool DoserDiagnosticsProjectionSource::read(
    Web::CoreDiagnosticsProjection& out) const {
    DoserDiagnosticsFacts facts {};
    if (!facts_.read(facts)) return false;
    Web::CoreDiagnosticsProjection value {};
    Diagnostics::DiagnosticsSnapshot& d = value.value;
    d.system.ready = facts.systemReady;
    d.system.identity = facts.identity;
    memcpy(d.system.aquaCoreVersion, facts.aquaCoreVersion,
           sizeof(d.system.aquaCoreVersion));
    d.system.uptimeMs = facts.uptimeMs;
    d.system.restartReason = facts.restartReason;
    d.systemHealth = facts.systemReady ? Diagnostics::HealthState::Ok
                                       : Diagnostics::HealthState::Error;
    d.time.rtcReady = facts.rtcConfigured && facts.rtcOk;
    d.time.rtcValid = facts.rtcConfigured && facts.rtcOk;
    d.time.ntpAvailable = facts.ntpConfigured;
    d.time.ntpInitialized = facts.ntpSynced;
    d.time.lastSyncResult = !facts.ntpConfigured
        ? Diagnostics::NtpSyncResult::NotAvailable
        : (facts.ntpSynced ? Diagnostics::NtpSyncResult::Success
                           : Diagnostics::NtpSyncResult::NotAttempted);
    d.time.state = !facts.timeEnabled ? Diagnostics::TimeState::Unavailable
                   : (facts.timeValid ? Diagnostics::TimeState::Valid
                                      : Diagnostics::TimeState::Invalid);
    const char* provider = facts.rtcConfigured && facts.rtcOk ? "RTC" :
                           (facts.ntpSynced ? "NTP" :
                            (facts.rtcConfigured ? "RTC unavailable" :
                             (facts.ntpConfigured ? "NTP pending" :
                              "Unavailable")));
    strncpy(d.time.providerName, provider, sizeof(d.time.providerName) - 1U);
    d.timeHealth = !facts.timeEnabled ? Diagnostics::HealthState::Unknown
                   : (facts.timeValid ? Diagnostics::HealthState::Ok
                                      : Diagnostics::HealthState::Warning);
    if (facts.rtcConfigured && !facts.rtcOk)
        d.timeHealth = worse(d.timeHealth, Diagnostics::HealthState::Warning);
    // Two product records have no single authoritative Core StorageStatus.
    // Preserve Unknown rather than inventing an OK state.
    d.storageHealth = Diagnostics::HealthState::Unknown;
    d.network.available = true;
    d.network.state = facts.networkState;
    d.network.staEnabled = facts.staEnabled;
    d.network.connected = facts.connected;
    d.network.apEnabled = facts.apEnabled;
    d.network.apActive = facts.apActive;
    d.network.apState = facts.apState;
    memcpy(d.network.ssid, facts.ssid, sizeof(d.network.ssid));
    memcpy(d.network.hostname, facts.hostname, sizeof(d.network.hostname));
    memcpy(d.network.apSsid, facts.apSsid, sizeof(d.network.apSsid));
    d.network.ipAddress = facts.ipAddress;
    d.network.apIpAddress = facts.apIpAddress;
    d.network.rssi = facts.rssi;
    d.network.reconnectCount = facts.reconnectCount;
    d.network.connectionUptimeMs = facts.connectionUptimeMs;
    if (facts.networkState == Network::NetworkState::Disabled ||
        facts.connected || (!facts.staEnabled && facts.apActive)) {
        d.networkHealth = Diagnostics::HealthState::Ok;
    } else if (facts.networkState == Network::NetworkState::Disconnected ||
               facts.networkState == Network::NetworkState::Error) {
        d.networkHealth = Diagnostics::HealthState::Warning;
    } else {
        d.networkHealth = Diagnostics::HealthState::Unknown;
    }
    d.overallHealth = worse(worse(d.systemHealth, d.timeHealth),
                            worse(worse(d.networkHealth, d.storageHealth),
                                  facts.productHealth));
    out = value;
    return true;
}
