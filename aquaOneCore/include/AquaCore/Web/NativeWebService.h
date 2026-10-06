#pragma once

#include "AquaCore/Web/CoreWebProjections.h"
#include "AquaCore/Web/HttpServerTransport.h"
#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/WebPageProvider.h"

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
    static constexpr size_t MAX_PAGE_PROVIDERS = 6U;
    NativeWebService(
        HttpServerTransport& transport,
        const PublishedSnapshot<CoreSystemProjection>& systemSnapshot,
        const PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsSnapshot
    );
    ~NativeWebService();

    // Product routes must be registered before the first begin() attempt.
    // The four Core paths remain reserved for the built-in read surface.
    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context = nullptr);
    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context,
                  const HttpRouteOptions& options);
    // Providers are borrowed and must outlive all callbacks. Providers may
    // render only static or projection-safe content in the HTTPD task.
    bool addPage(WebPageProvider& provider);
    bool begin(const WebConfig& config);
    void stop();
    bool isRunning() const;
    NativeWebState state() const { return state_; }
    bool routesRegistered() const { return routesRegistered_; }

private:
    bool registerRoutes();
    static void handleRoot(void*, const HttpRouteRequest&, WebResponseWriter&);
    static void handlePage(void*, const HttpRouteRequest&, WebResponseWriter&);
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
    bool registrationClosed_ = false;
    struct PageRoute {
        NativeWebService* service = nullptr;
        WebPageProvider* provider = nullptr;
    };
    WebPageProvider* rootPage_ = nullptr;
    PageRoute pageRoutes_[MAX_PAGE_PROVIDERS] {};
    size_t pageRouteCount_ = 0U;
};

} // namespace Web
} // namespace AquaCore
