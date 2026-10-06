#pragma once

#include <stdint.h>
#include <type_traits>

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Web/NativeWebService.h"
#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/WebApplicationBridge.h"

#include "../app/FirmwareApp.h"
#include "LumaPages.h"
#include "LumaWebProtocol.h"

namespace LumaSense {
namespace Web {

struct LumaStatusProjection {
    OperatingMode mode = OperatingMode::Normal;
    uint8_t activeProfile = 1U;
    char activeProfileName[32] {};
    DayState dayState = DayState::Day;
    LocalTime localTime {};
    float requestedLevels[CHANNEL_COUNT] {};
    float finalLevels[CHANNEL_COUNT] {};
    float globalPowerLimit = 0.0f;
    bool timeValid = false;
    AquaCore::Network::NetworkState wifiState =
        AquaCore::Network::NetworkState::Disabled;
    bool wifiConnected = false;
    AquaCore::Network::IpAddress wifiIp {};
    int32_t wifiRssi = 0;
    AquaCore::Diagnostics::HealthState overallHealth =
        AquaCore::Diagnostics::HealthState::Unknown;
};

static_assert(std::is_trivially_copyable<LumaStatusProjection>::value,
              "Luma status projection must remain a copied value");

using LumaApplicationBridge = AquaCore::Web::WebApplicationBridge<
    LumaWebRequest,
    FirmwareCommandResult,
    4U
>;

// Application-side transitional adapter. It is the only Web component which
// may borrow mutable Luma authority. processOne() executes at most one request.
class LumaWebApplication {
public:
    LumaWebApplication(
        FirmwareApp& app,
        const AquaCore::Network::NetworkService& network,
        const AquaCore::Diagnostics::DiagnosticsService& diagnostics,
        AquaCore::Web::PublishedSnapshot<LumaStatusProjection>& statusTarget,
        LumaApplicationBridge& bridge
    );

    bool processOne(uint32_t nowMs);
    bool publishStatus();

private:
    static FirmwareCommandResult execute(
        const LumaWebRequest& request, void* context
    );

    FirmwareApp& app_;
    const AquaCore::Network::NetworkService& network_;
    const AquaCore::Diagnostics::DiagnosticsService& diagnostics_;
    AquaCore::Web::PublishedSnapshot<LumaStatusProjection>& statusTarget_;
    LumaApplicationBridge& bridge_;
    uint32_t executionNowMs_ = 0U;
};

// HTTPD-side route composition. It owns only static pages, a published value
// and the typed bridge; it has no access to Application/Domain authorities.
class LumaNativeWeb {
public:
    static constexpr uint32_t ACTION_WAIT_TIMEOUT_MS = 250U;

    LumaNativeWeb(
        AquaCore::Web::NativeWebService& web,
        const AquaCore::Web::PublishedSnapshot<LumaStatusProjection>& status,
        LumaApplicationBridge& bridge
    );

    bool registerRoutes();

private:
    using Parser = bool (*)(
        const char*, size_t, LumaWebRequest&, const char*&
    );

    static void handleStatus(
        void*, const AquaCore::Web::HttpRouteRequest&,
        AquaCore::Web::WebResponseWriter&
    );
    static void handleMode(
        void*, const AquaCore::Web::HttpRouteRequest&,
        AquaCore::Web::WebResponseWriter&
    );
    static void handleProfile(
        void*, const AquaCore::Web::HttpRouteRequest&,
        AquaCore::Web::WebResponseWriter&
    );
    static void handleManual(
        void*, const AquaCore::Web::HttpRouteRequest&,
        AquaCore::Web::WebResponseWriter&
    );
    void handleAction(
        const AquaCore::Web::HttpRouteRequest& request,
        AquaCore::Web::WebResponseWriter& response,
        Parser parser
    );

    AquaCore::Web::NativeWebService& web_;
    const AquaCore::Web::PublishedSnapshot<LumaStatusProjection>& status_;
    LumaApplicationBridge& bridge_;
    bool registered_ = false;

    DashboardPage dashboardPage_;
    ControlPage controlPage_;
    DiagnosticsPage diagnosticsPage_;
    SystemPage systemPage_;
};

} // namespace Web
} // namespace LumaSense
