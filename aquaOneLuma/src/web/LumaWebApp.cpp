#include "LumaWebApp.h"

namespace LumaSense {
namespace Web {

LumaWebApp::LumaWebApp(
    AquaCore::Web::WebService& web,
    FirmwareApp& app,
    AquaCore::Network::NetworkService& network,
    AquaCore::Diagnostics::DiagnosticsService& diagnostics
)
    : web_(web),
      statusApi_(app, network, diagnostics),
      modeApi_(app),
      profileApi_(app),
      manualApi_(app, nowMs_) {
}

bool LumaWebApp::registerRoutes() {
    if (registered_) {
        return true;
    }

    if (
        !web_.addPage(dashboardPage_) ||
        !web_.addPage(controlPage_) ||
        !web_.addPage(diagnosticsPage_) ||
        !web_.addPage(systemPage_) ||
        !web_.addApi(statusApi_) ||
        !web_.addApi(modeApi_) ||
        !web_.addApi(profileApi_) ||
        !web_.addApi(manualApi_)
    ) {
        return false;
    }

    registered_ = true;
    return true;
}

void LumaWebApp::update(uint32_t nowMs) {
    nowMs_ = nowMs;
}

} // namespace Web
} // namespace LumaSense
