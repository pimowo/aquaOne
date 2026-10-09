#include "HydroNativeWeb.h"

#include "web/HydroWebProtocol.h"

using namespace AquaCore::Web;

namespace
{

void writePlain(WebResponseWriter& response, uint16_t status, const char* body)
{
    response.beginResponse(status, ContentType::PlainText);
    response.writeText(body);
    response.endResponse();
}

}

HydroNativeWeb::HydroNativeWeb(
    NativeWebService& web,
    const PublishedSnapshot<SystemStatus>& status,
    const PublishedSnapshot<HydroSettingsProjection>& settings,
    const PublishedSnapshot<HydroDiagnosticsProjection>& diagnostics,
    HydroApplicationBridge& bridge
) : web_(web), bridge_(bridge), dashboard_(status), controlPage_(status),
    settingsPage_(settings), diagnosticsPage_(diagnostics), api_(status)
{
}

HydroNativeWeb::~HydroNativeWeb()
{
    // Stop before the route context and owned pages are destroyed.
    web_.stop();
}

bool HydroNativeWeb::registerRoutes()
{
    if (registered_) return true;
    if (!web_.addPage(dashboard_) ||
        !web_.addPage(controlPage_) ||
        !web_.addPage(settingsPage_) ||
        !web_.addPage(diagnosticsPage_) ||
        !web_.addRoute("/api/hydrosense", HttpMethod::Get,
                       HydroSenseApi::handle, &api_) ||
        !web_.addRoute("/api/control", HttpMethod::Post,
                       handleControl, this, HttpRouteOptions {CONTROL_BODY_LIMIT}) ||
        !web_.addRoute("/api/settings", HttpMethod::Post,
                       handleSettings, this, HttpRouteOptions {SETTINGS_BODY_LIMIT}))
    {
        return false;
    }
    registered_ = true;
    return true;
}

void HydroNativeWeb::handleControl(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
)
{
    HydroNativeWeb* self = static_cast<HydroNativeWeb*>(context);
    HydroWebRequest typed {};
    if (self == nullptr ||
        !parseHydroControlRequest(request.body, request.bodyLength, typed))
    {
        writePlain(response, 400U, "Niepoprawna akcja");
        return;
    }
    self->submit(typed, false, response);
}

void HydroNativeWeb::handleSettings(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
)
{
    HydroNativeWeb* self = static_cast<HydroNativeWeb*>(context);
    HydroWebRequest typed {};
    typed.kind = HydroWebRequestKind::SaveSettings;
    if (self == nullptr ||
        !parseHydroSettingsRequest(request.body, request.bodyLength, typed.settings))
    {
        writePlain(response, 400U, "Niepoprawne dane formularza");
        return;
    }
    self->submit(typed, true, response);
}

void HydroNativeWeb::submit(
    const HydroWebRequest& request, bool settings, WebResponseWriter& response
)
{
    ApplicationBridgeToken token {};
    if (bridge_.submit(request, token) != ApplicationBridgeSubmitResult::Accepted)
    {
        writePlain(response, 503U, "Service Unavailable");
        return;
    }

    HydroWebResult result = HydroWebResult::ValidationFailure;
    const ApplicationBridgeWaitResult waited = bridge_.wait(
        token, ACTION_WAIT_TIMEOUT_MS, result);
    if (waited == ApplicationBridgeWaitResult::Completed)
    {
        if (result == HydroWebResult::CompletedSuccess)
            writePlain(response, 200U, "OK");
        else if (result == HydroWebResult::StorageFailure && settings)
            writePlain(response, 500U, "Blad zapisu konfiguracji");
        else
            writePlain(response, 400U, "Konfiguracja odrzucona przez walidator");
        return;
    }

    if (waited != ApplicationBridgeWaitResult::TimedOutAccepted)
        (void)bridge_.abandon(token);
    writePlain(response, 202U, "OUTCOME UNKNOWN - sprawdz stan urzadzenia");
}
