#include "DoserDiagnosticsProjection.h"

#include "DiagnosticsManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"

#include <string.h>

bool DoserManagerDiagnosticsFacts::read(DoserDiagnosticsFacts& out) const {
    DoserDiagnosticsFacts facts {};
    facts.systemReady = system_.isReady();
    facts.identity = system_.deviceIdentity();
    strncpy(facts.aquaCoreVersion, system_.aquaCoreVersion(),
            sizeof(facts.aquaCoreVersion) - 1U);
    facts.uptimeMs = system_.uptimeMs();
    facts.restartReason = system_.restartReason();
    facts.rtcConfigured = time_.isRtcConfigured();
    facts.rtcOk = time_.isRtcOk();
    facts.timeEnabled = time_.isTimeEnabled();
    facts.timeValid = time_.isTimeValid();
    facts.ntpConfigured = time_.isNtpConfigured();
    facts.ntpSynced = time_.isNtpSynced();
    const AquaCore::Network::NetworkService& network = wifi_.service();
    facts.networkState = network.state();
    facts.staEnabled = network.isStaEnabled();
    facts.connected = network.isConnected();
    facts.apEnabled = network.isApEnabled();
    facts.apActive = network.isApActive();
    facts.apState = network.accessPointState();
    strncpy(facts.ssid, network.ssid(), sizeof(facts.ssid) - 1U);
    strncpy(facts.hostname, network.hostname(), sizeof(facts.hostname) - 1U);
    strncpy(facts.apSsid, network.accessPointSsid(), sizeof(facts.apSsid) - 1U);
    facts.ipAddress = network.ipAddress();
    facts.apIpAddress = network.accessPointIpAddress();
    facts.rssi = network.rssi();
    facts.reconnectCount = network.reconnectCount();
    facts.connectionUptimeMs = static_cast<uint32_t>(
        network.connectionUptimeMs(system_.uptimeMs()));
    switch (diagnostics_.getSystemStatus()) {
        case DiagnosticsManager::SystemStatus::OK:
            facts.productHealth = AquaCore::Diagnostics::HealthState::Ok; break;
        case DiagnosticsManager::SystemStatus::WARNING:
            facts.productHealth = AquaCore::Diagnostics::HealthState::Warning; break;
        case DiagnosticsManager::SystemStatus::ERROR:
            facts.productHealth = AquaCore::Diagnostics::HealthState::Error; break;
    }
    out = facts;
    return true;
}
