#pragma once

#include <AquaCore/Web/WebApiProvider.h>

#include "hydrosense/SystemStatus.h"

class HydroSenseApi final
    : public AquaCore::Web::WebApiProvider
{
public:
    explicit HydroSenseApi(
        const SystemStatus& status
    );

    const char* route() const override;

    AquaCore::Web::HttpMethod
    method() const override;

    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

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

    const SystemStatus& status_;
};