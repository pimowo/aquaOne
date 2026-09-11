#pragma once

#include <AquaCore/Web/WebPageProvider.h>

#include "hydrosense/HydroSenseConfig.h"

class HydroSenseSettingsPage final
    : public AquaCore::Web::WebPageProvider
{
public:
    explicit HydroSenseSettingsPage(
        const HydroSenseConfig& config
    );

    const char* route() const override;
    const char* title() const override;

    void render(
        AquaCore::Web::WebResponseWriter& response
    ) const override;

private:
    static void writeChecked(
        AquaCore::Web::WebResponseWriter& response,
        bool value
    );

    static void writeValue(
        AquaCore::Web::WebResponseWriter& response,
        const char* value
    );

    const HydroSenseConfig& config_;
};