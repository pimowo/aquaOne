#pragma once

#include <stdint.h>
#include "AquaCore/Web/WebTypes.h"

namespace AquaCore {
namespace Web {

class WebBackend {
public:
    virtual ~WebBackend() = default;
    virtual bool addRoute(
        const char* path,
        HttpMethod method,
        WebRouteHandler handler,
        void* context
    ) = 0;
    virtual bool setNotFoundHandler(
        WebRouteHandler handler,
        void* context
    ) = 0;
    virtual bool begin(uint16_t port) = 0;
    virtual void update() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

} // namespace Web
} // namespace AquaCore
