#pragma once

#include <AquaCore/Web/NativeWebService.h>
#include <AquaCore/Web/PublishedSnapshot.h>

#include "web/HydroSenseApi.h"
#include "web/HydroSenseControlPage.h"
#include "web/HydroSenseDashboard.h"
#include "web/HydroSenseDiagnosticsPage.h"
#include "web/HydroSenseSettingsPage.h"
#include "web/HydroWebTypes.h"

class HydroNativeWeb
{
public:
    static constexpr uint32_t ACTION_WAIT_TIMEOUT_MS = 1000U;
    static constexpr size_t CONTROL_BODY_LIMIT = 32U;
    static constexpr size_t SETTINGS_BODY_LIMIT = 1536U;

    HydroNativeWeb(
        AquaCore::Web::NativeWebService& web,
        const AquaCore::Web::PublishedSnapshot<SystemStatus>& status,
        const AquaCore::Web::PublishedSnapshot<HydroSettingsProjection>& settings,
        const AquaCore::Web::PublishedSnapshot<HydroDiagnosticsProjection>& diagnostics,
        HydroApplicationBridge& bridge
    );

    bool registerRoutes();

private:
    static void handleControl(
        void* context,
        const AquaCore::Web::HttpRouteRequest& request,
        AquaCore::Web::WebResponseWriter& response
    );
    static void handleSettings(
        void* context,
        const AquaCore::Web::HttpRouteRequest& request,
        AquaCore::Web::WebResponseWriter& response
    );
    void submit(
        const HydroWebRequest& request,
        bool settings,
        AquaCore::Web::WebResponseWriter& response
    );

    AquaCore::Web::NativeWebService& web_;
    HydroApplicationBridge& bridge_;
    bool registered_ = false;
    HydroSenseDashboard dashboard_;
    HydroSenseControlPage controlPage_;
    HydroSenseSettingsPage settingsPage_;
    HydroSenseDiagnosticsPage diagnosticsPage_;
    HydroSenseApi api_;
};
