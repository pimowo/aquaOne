#include "DoserNativeWeb.h"
#include "DoserUpdatePage.h"

#include <string.h>

using namespace AquaCore::Web;

namespace {
void sendText(WebResponseWriter& response, uint16_t status,
              const char* body) {
    response.beginResponse(status, ContentType::PlainText);
    response.writeText(body);
    response.endResponse();
}
}

DoserWebResult DoserWebApplication::execute(const DoserWebRequest& request,
                                             void* context) {
    DoserWebApplication& self = *static_cast<DoserWebApplication*>(context);
    if (request.kind != DoserWebRequestKind::ScheduleRestart)
        return DoserWebResult::Rejected;
    self.restartPending_ = true;
    self.restartAt_ = self.authority_.nowMs() + 1000U;
    return DoserWebResult::Scheduled;
}

bool DoserWebApplication::processOne() {
    return bridge_.processOne(execute, this);
}

void DoserWebApplication::serviceRestart() {
    if (!restartPending_ ||
        static_cast<int32_t>(authority_.nowMs() - restartAt_) < 0) return;
    restartPending_ = false;
    authority_.stopPumps();
    authority_.restartDevice();
}

bool DoserNativeWebRoutes::authenticateAdmin(
    const HttpRouteRequest& request, WebResponseWriter& response) const {
    if (password_ == nullptr || password_[0] == '\0' ||
        strcmp(password_, "CHANGE_ME_BEFORE_USE") == 0) {
        sendText(response, 503U,
                 "Funkcje administracyjne są wyłączone. Ustaw WEB_PASS w secrets.h.");
        return false;
    }
    if (request.authenticateBasic(user_, password_)) return true;
    if (!request.requestBasicAuthentication("PMW AquaDoser"))
        sendText(response, 500U, "Authentication unavailable");
    return false;
}

bool DoserNativeWebRoutes::addTo(NativeWebService& service) {
    return service.addRoute("/api/restart", HttpMethod::Post, restartRoute,
                            this, HttpRouteOptions(0U)) &&
           service.addRoute("/update", HttpMethod::Get, updateRoute,
                            this, HttpRouteOptions(0U));
}

void DoserNativeWebRoutes::restartRoute(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response) {
    DoserNativeWebRoutes& self = *static_cast<DoserNativeWebRoutes*>(context);
    if (!self.authenticateAdmin(request, response)) return;
    ApplicationBridgeToken token {};
    const DoserWebRequest command {DoserWebRequestKind::ScheduleRestart};
    const ApplicationBridgeSubmitResult submitted = self.bridge_.submit(command, token);
    if (submitted != ApplicationBridgeSubmitResult::Accepted) {
        sendText(response, 503U, "Restart unavailable");
        return;
    }
    DoserWebResult result = DoserWebResult::Rejected;
    // Provisional Doser route wait, not a Core-wide timeout policy.
    const ApplicationBridgeWaitResult waited = self.bridge_.wait(token, 1000U, result);
    if (waited == ApplicationBridgeWaitResult::Completed &&
        result == DoserWebResult::Scheduled) {
        sendText(response, 202U,
                 "Restart zaplanowany. Urządzenie uruchomi się ponownie.");
    } else if (waited == ApplicationBridgeWaitResult::Completed) {
        sendText(response, 503U, "Restart unavailable");
    } else {
        // Submission was accepted; completion uncertainty cannot cancel it.
        sendText(response, 202U, "Restart accepted; outcome unknown.");
    }
}

void DoserNativeWebRoutes::updateRoute(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response) {
    DoserNativeWebRoutes& self = *static_cast<DoserNativeWebRoutes*>(context);
    if (!self.authenticateAdmin(request, response)) return;
    response.beginResponse(200U, ContentType::Html);
    response.write(DOSER_UPDATE_PAGE, strlen(DOSER_UPDATE_PAGE));
    response.endResponse();
}
