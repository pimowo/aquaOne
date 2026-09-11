#pragma once

#include <AquaCore/Web/WebPageProvider.h>

#include "hydrosense/SystemStatus.h"

class HydroSenseControlPage final
    : public AquaCore::Web::WebPageProvider
{
public:
    explicit HydroSenseControlPage(
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

    const SystemStatus& status_;
};