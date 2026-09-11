#pragma once

#include <AquaCore/Web/WebPageProvider.h>
#include <AquaCore/System/SystemService.h>
#include <AquaCore/Network/NetworkService.h>

#include "hydrosense/SystemStatus.h"
#include "hydrosense/HydroSenseConfigStorage.h"

class HydroSenseDiagnosticsPage final
    : public AquaCore::Web::WebPageProvider
{
public:
    HydroSenseDiagnosticsPage(
        const AquaCore::SystemService& systemService,
        const AquaCore::Network::NetworkService& networkService,
        const HydroSenseConfigStorage& configStorage,
        const SystemStatus& status
    );

    const char* route() const override;
    const char* title() const override;

    void render(
        AquaCore::Web::WebResponseWriter& response
    ) const override;

private:
    static const char* networkStateName(
        AquaCore::Network::NetworkState state
    );

    static const char* storageSlotName(
        AquaCore::Config::StorageSlot slot
    );

    static const char* storageResultName(
        AquaCore::Config::StorageOperationResult result
    );

    static const char* topupStateName(
        TopupController::State state
    );

    static const char* alarmCodeName(
        AlarmManager::Code code
    );

    static const char* severityName(
        AlarmManager::Severity severity
    );

    static void writeIp(
        AquaCore::Web::WebResponseWriter& response,
        const AquaCore::Network::IpAddress& ip
    );

    static void writeUptime(
        AquaCore::Web::WebResponseWriter& response,
        uint32_t uptimeMs
    );

    const AquaCore::SystemService&
        systemService_;

    const AquaCore::Network::NetworkService&
        networkService_;

    const HydroSenseConfigStorage&
        configStorage_;

    const SystemStatus&
        status_;
};