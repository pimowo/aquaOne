#pragma once

#include <stdint.h>

#include "AquaCore/Web/HttpRouteRegistry.h"

namespace AquaCore {
namespace Web {

// Narrow, borrowed HTTP registration/lifecycle capability. HTTP servicing is
// owned by the concrete server task; this contract deliberately has no poll,
// Realtime, upload, Auth or product action methods.
class HttpServerTransport {
public:
    virtual bool addRoute(const char* path, HttpMethod method,
                          HttpRouteHandler handler, void* context = nullptr) {
        return addRoute(path, method, handler, context, HttpRouteOptions {});
    }
    virtual bool addRoute(const char* path, HttpMethod method,
                          HttpRouteHandler handler, void* context,
                          const HttpRouteOptions& options) = 0;
    virtual bool setNotFoundHandler(HttpNotFoundHandler handler,
                                    void* context = nullptr) = 0;
    virtual bool begin(uint16_t port) = 0;
    // Must complete teardown before returning. An implementation unable to
    // do so must fail fast: route contexts may be destroyed immediately after.
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

protected:
    ~HttpServerTransport() = default;
};

} // namespace Web
} // namespace AquaCore
