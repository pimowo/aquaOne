#pragma once

#include <AquaCore/Web/WebPageProvider.h>

#include "hydrosense/SystemStatus.h"

class HydroSenseDashboard final
    : public AquaCore::Web::WebPageProvider
{
public:
    explicit HydroSenseDashboard(
        const SystemStatus& status
    );

    const char* route() const override;
    const char* title() const override;

    void render(
        AquaCore::Web::WebResponseWriter& response
    ) const override;

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