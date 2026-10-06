#pragma once

#include <stddef.h>
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/WebPageProvider.h"

namespace AquaCore {
namespace Web {

struct WebShellInfo {
    static constexpr size_t CORE_VERSION_CAPACITY = 16U;
    DeviceIdentity identity {};
    char aquaCoreVersion[CORE_VERSION_CAPACITY] {};
    bool ready = false;
};

class HtmlShell {
public:
    static bool render(
        WebResponseWriter& response,
        const SystemService* system,
        const WebConfig& config,
        const char* pageTitle,
        const WebPageProvider* page = nullptr
    );
    static bool render(
        WebResponseWriter& response,
        const WebShellInfo& info,
        const WebConfig& config,
        const char* pageTitle,
        const WebPageProvider* page = nullptr
    );
    static const char* stylesheet();
    static size_t stylesheetLength();
};

} // namespace Web
} // namespace AquaCore
