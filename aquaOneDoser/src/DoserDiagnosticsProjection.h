#pragma once

#include <AquaCore/Web/CoreWebProjections.h>
#include <AquaCore/System/SystemService.h>
#include <AquaCore/Network/NetworkService.h>

class TimeManager;
class WiFiManager;
class DiagnosticsManager;

struct DoserDiagnosticsFacts {
    bool systemReady = false;
    AquaCore::DeviceIdentity identity {};
    char aquaCoreVersion[AquaCore::Diagnostics::VERSION_TEXT_CAPACITY] {};
    uint32_t uptimeMs = 0U;
    AquaCore::RestartReason restartReason = AquaCore::RestartReason::Unknown;
    bool rtcConfigured = false;
    bool rtcOk = false;
    bool timeEnabled = false;
    bool timeValid = false;
    bool ntpConfigured = false;
    bool ntpSynced = false;
    AquaCore::Network::NetworkState networkState =
        AquaCore::Network::NetworkState::Disabled;
    bool staEnabled = false;
    bool connected = false;
    bool apEnabled = false;
    bool apActive = false;
    AquaCore::Network::AccessPointState apState =
        AquaCore::Network::AccessPointState::Disabled;
    char ssid[AquaCore::Network::WIFI_SSID_CAPACITY] {};
    char hostname[AquaCore::Network::WIFI_HOSTNAME_CAPACITY] {};
    char apSsid[AquaCore::Network::WIFI_SSID_CAPACITY] {};
    AquaCore::Network::IpAddress ipAddress {};
    AquaCore::Network::IpAddress apIpAddress {};
    int32_t rssi = 0;
    uint32_t reconnectCount = 0U;
    uint32_t connectionUptimeMs = 0U;
    AquaCore::Diagnostics::HealthState productHealth =
        AquaCore::Diagnostics::HealthState::Unknown;
};

class DoserDiagnosticsFactsSource {
public:
    virtual ~DoserDiagnosticsFactsSource() = default;
    virtual bool read(DoserDiagnosticsFacts& out) const = 0;
};

// Called only from the serialized Application context before publication.
class DoserDiagnosticsProjectionSource final
    : public AquaCore::Web::CoreDiagnosticsProjectionSource {
public:
    explicit DoserDiagnosticsProjectionSource(
        const DoserDiagnosticsFactsSource& facts) : facts_(facts) {}
    bool read(AquaCore::Web::CoreDiagnosticsProjection& out) const override;

private:
    const DoserDiagnosticsFactsSource& facts_;
};

// Concrete borrowed-manager adapter. No reference to these authorities is
// passed into HTTPD; the projection source copies into PublishedSnapshot.
class DoserManagerDiagnosticsFacts final : public DoserDiagnosticsFactsSource {
public:
    DoserManagerDiagnosticsFacts(const AquaCore::SystemService& system,
                                 const TimeManager& time,
                                 const WiFiManager& wifi,
                                 const DiagnosticsManager& diagnostics)
        : system_(system), time_(time), wifi_(wifi), diagnostics_(diagnostics) {}
    bool read(DoserDiagnosticsFacts& out) const override;

private:
    const AquaCore::SystemService& system_;
    const TimeManager& time_;
    const WiFiManager& wifi_;
    const DiagnosticsManager& diagnostics_;
};
