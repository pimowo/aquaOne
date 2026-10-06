#pragma once

#include "AquaCore/Web/CoreWebProjections.h"
#include "AquaCore/Web/HttpServerTransport.h"
#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/WebConfig.h"

namespace AquaCore {
namespace Web {

enum class NativeWebState : uint8_t {
    Disabled,
    Stopped,
    Running,
    Failed
};

// Application-owned native Core Web integration. Transport and snapshots are
// borrowed and must outlive this service. Destruction stops the transport so
// no callback remains active; because routes retain this context, the borrowed
// transport must not be restarted after the service is destroyed.
class NativeWebService {
public:
    NativeWebService(
        HttpServerTransport& transport,
        const PublishedSnapshot<CoreSystemProjection>& systemSnapshot,
        const PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsSnapshot
    );
    ~NativeWebService();

    bool begin(const WebConfig& config);
    void stop();
    bool isRunning() const;
    NativeWebState state() const { return state_; }
    bool routesRegistered() const { return routesRegistered_; }

private:
    bool registerRoutes();
    static void handleRoot(void*, const HttpRouteRequest&, WebResponseWriter&);
    static void handleStylesheet(void*, const HttpRouteRequest&, WebResponseWriter&);
    static void handleSystem(void*, const HttpRouteRequest&, WebResponseWriter&);
    static void handleDiagnostics(void*, const HttpRouteRequest&, WebResponseWriter&);
    static void handleNotFound(void*, WebResponseWriter&);

    HttpServerTransport& transport_;
    const PublishedSnapshot<CoreSystemProjection>& systemSnapshot_;
    const PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsSnapshot_;
    WebConfig config_ {};
    NativeWebState state_ = NativeWebState::Stopped;
    bool routesRegistered_ = false;
    bool routeRegistrationFailed_ = false;
};

} // namespace Web
} // namespace AquaCore
