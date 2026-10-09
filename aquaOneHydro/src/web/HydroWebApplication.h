#pragma once

#include <AquaCore/Network/NetworkService.h>
#include <AquaCore/System/SystemService.h>
#include <AquaCore/Web/CoreWebProjectionSources.h>
#include <AquaCore/Web/PublishedSnapshot.h>

#include "web/HydroWebAuthorities.h"
#include "web/HydroWebTypes.h"

class HydroCoreDiagnosticsSource final
    : public AquaCore::Web::CoreDiagnosticsProjectionSource
{
public:
    HydroCoreDiagnosticsSource(
        const AquaCore::SystemService& system,
        const AquaCore::Network::NetworkService& network,
        const HydroConfigPersistence& storage
    );

    bool read(
        AquaCore::Web::CoreDiagnosticsProjection& out
    ) const override;

private:
    const AquaCore::SystemService& system_;
    const AquaCore::Network::NetworkService& network_;
    const HydroConfigPersistence& storage_;
};

class HydroWebApplication
{
public:
    HydroWebApplication(
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
    );

    bool processOne();
    bool publish();
    bool restartRequested() const;
    void clearRestartRequest();

private:
    static HydroWebResult execute(
        const HydroWebRequest& request,
        void* context
    );
    HydroWebResult applySettings(
        const HydroSettingsRequest& request
    );

    HydroSenseConfig& config_;
    HydroConfigPersistence& storage_;
    HydroTopupActions& topupController_;
    HydroBuzzerActions& buzzerController_;
    const SystemStatus& status_;
    const AquaCore::SystemService& system_;
    AquaCore::Web::PublishedSnapshot<SystemStatus>& statusTarget_;
    AquaCore::Web::PublishedSnapshot<HydroSettingsProjection>& settingsTarget_;
    AquaCore::Web::PublishedSnapshot<HydroDiagnosticsProjection>& diagnosticsTarget_;
    HydroApplicationBridge& bridge_;
    bool restartRequested_ = false;
};
