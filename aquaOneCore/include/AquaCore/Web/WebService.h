#pragma once

#include <stddef.h>
#include <stdint.h>
#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/WebApiProvider.h"
#include "AquaCore/Web/WebBackend.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/WebPageProvider.h"

namespace AquaCore {
namespace Web {

class WebService {
public:
    static constexpr size_t MAX_PAGE_PROVIDERS = 8U;
    static constexpr size_t MAX_API_PROVIDERS = 8U;

    explicit WebService(WebBackend& backend);
    WebService(
        WebBackend& backend,
        const SystemService& system,
        const Diagnostics::DiagnosticsService* diagnostics = nullptr
    );

    bool begin(const WebConfig& config);
    void update();
    void stop();
    bool isRunning() const;

    bool addRoute(
        const char* path,
        HttpMethod method,
        WebRouteHandler handler,
        void* context = nullptr
    );
    bool addPage(WebPageProvider& provider);
    bool addApi(WebApiProvider& provider);

private:
    struct PageRegistration {
        WebService* service = nullptr;
        WebPageProvider* provider = nullptr;
    };
    struct ApiRegistration {
        WebApiProvider* provider = nullptr;
    };

    WebBackend& backend_;
    const SystemService* system_;
    const Diagnostics::DiagnosticsService* diagnostics_;
    WebPageProvider* rootPage_ = nullptr;
    WebConfig config_ {};
    bool running_ = false;
    uint8_t defaultRegistrationStep_ = 0U;
    PageRegistration pages_[MAX_PAGE_PROVIDERS] {};
    ApiRegistration apis_[MAX_API_PROVIDERS] {};
    size_t pageCount_ = 0U;
    size_t apiCount_ = 0U;

    bool registerDefaultRoutes();
    static bool isValidPath(const char* path);

    static void handleRoot(void*, const WebRequest&, WebResponseWriter&);
    static void handleStylesheet(void*, const WebRequest&, WebResponseWriter&);
    static void handleSystemApi(void*, const WebRequest&, WebResponseWriter&);
    static void handleDiagnosticsApi(void*, const WebRequest&, WebResponseWriter&);
    static void handleNotFound(void*, const WebRequest&, WebResponseWriter&);
    static void handlePage(void*, const WebRequest&, WebResponseWriter&);
    static void handleApi(void*, const WebRequest&, WebResponseWriter&);
};

} // namespace Web
} // namespace AquaCore
