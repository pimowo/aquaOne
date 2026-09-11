#pragma once

#include "AquaCore/Web/WebTypes.h"

namespace AquaCore {
namespace Web {

class WebPageProvider {
public:
    virtual ~WebPageProvider() = default;
    virtual const char* route() const = 0;
    virtual const char* title() const = 0;
    virtual void render(WebResponseWriter& response) const = 0;
};

} // namespace Web
} // namespace AquaCore
