#pragma once

#include <AquaCore/Web/PublishedSnapshot.h>
#include <AquaCore/Web/HttpRouteRegistry.h>

#include "hydrosense/SystemStatus.h"

class HydroSenseApi final
{
public:
    explicit HydroSenseApi(
        const AquaCore::Web::PublishedSnapshot<SystemStatus>& status
    );

    static void handle(
        void* context,
        const AquaCore::Web::HttpRouteRequest& request,
        AquaCore::Web::WebResponseWriter& response
    );

private:
    static const char* topupStateName(
        TopupController::State state
    );

    static const char* reserveStateName(
        WaterReserve::State state
    );

    static const char* alarmCodeName(
        AlarmManager::Code code
    );

    static const char* severityName(
        AlarmManager::Severity severity
    );

    const AquaCore::Web::PublishedSnapshot<SystemStatus>& status_;
};
