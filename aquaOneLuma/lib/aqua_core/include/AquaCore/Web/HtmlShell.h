#pragma once

#include <stddef.h>
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/WebPageProvider.h"

namespace AquaCore {
namespace Web {

class HtmlShell {
public:
    static bool render(
        WebResponseWriter& response,
        const SystemService* system,
        const WebConfig& config,
        const char* pageTitle,
        const WebPageProvider* page = nullptr
    );
    static const char* stylesheet();
    static size_t stylesheetLength();
};

} // namespace Web
} // namespace AquaCore
