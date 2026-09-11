#pragma once

#include "AquaCore/Web/WebTypes.h"

namespace AquaCore {
namespace Web {

class WebApiProvider {
public:
    virtual ~WebApiProvider() = default;
    virtual const char* route() const = 0;
    virtual HttpMethod method() const = 0;
    virtual void handle(
        const WebRequest& request,
        WebResponseWriter& response
    ) = 0;
};

} // namespace Web
} // namespace AquaCore
