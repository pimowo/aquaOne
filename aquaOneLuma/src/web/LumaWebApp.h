#pragma once

#include <stdint.h>

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Web/WebService.h"

#include "../app/FirmwareApp.h"
#include "LumaApi.h"
#include "LumaPages.h"

namespace LumaSense {
namespace Web {

class LumaWebApp {
public:
    LumaWebApp(
        AquaCore::Web::WebService& web,
        FirmwareApp& app,
        AquaCore::Network::NetworkService& network,
        AquaCore::Diagnostics::DiagnosticsService& diagnostics
    );

    bool registerRoutes();
    void update(uint32_t nowMs);

private:
    AquaCore::Web::WebService& web_;
    uint32_t nowMs_ = 0U;
    bool registered_ = false;

    DashboardPage dashboardPage_;
    ControlPage controlPage_;
    DiagnosticsPage diagnosticsPage_;
    SystemPage systemPage_;

    StatusApi statusApi_;
    ModeApi modeApi_;
    ProfileApi profileApi_;
    ManualApi manualApi_;
};

} // namespace Web
} // namespace LumaSense
