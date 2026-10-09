#include "LumaNativeWeb.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "AquaCore/Web/HttpRouteRegistry.h"

namespace LumaSense {
namespace Web {
namespace {

using namespace AquaCore::Web;

const char* nativeModeName(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Normal: return "NORMAL";
        case OperatingMode::Service: return "SERVICE";
        case OperatingMode::Manual: return "MANUAL";
        case OperatingMode::ChannelTest: return "CHANNEL_TEST";
        case OperatingMode::Preview: return "PREVIEW";
        case OperatingMode::Simulation: return "SIMULATION";
        case OperatingMode::Off: return "OFF";
    }
    return "UNKNOWN";
}

const char* nativeNetworkStateName(AquaCore::Network::NetworkState state) {
    using AquaCore::Network::NetworkState;
    switch (state) {
        case NetworkState::Disabled: return "disabled";
        case NetworkState::Idle: return "idle";
        case NetworkState::Connecting: return "connecting";
        case NetworkState::Connected: return "connected";
        case NetworkState::Disconnected: return "disconnected";
        case NetworkState::Error: return "error";
    }
    return "unknown";
}

const char* nativeHealthName(AquaCore::Diagnostics::HealthState state) {
    using AquaCore::Diagnostics::HealthState;
    switch (state) {
        case HealthState::Ok: return "ok";
        case HealthState::Unknown: return "unknown";
        case HealthState::Warning: return "warning";
        case HealthState::Error: return "error";
    }
    return "unknown";
}

bool writeNativeUnsigned(WebResponseWriter& response, uint32_t value) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%lu", static_cast<unsigned long>(value)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

bool writeNativeSigned(WebResponseWriter& response, int32_t value) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%ld", static_cast<long>(value)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

bool writeNativeFloat(WebResponseWriter& response, float value) {
    if (!std::isfinite(value)) value = 0.0f;
    char text[20] {};
    const int length = std::snprintf(
        text, sizeof(text), "%.2f", static_cast<double>(value)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

bool writeNativeIp(WebResponseWriter& response,
             const AquaCore::Network::IpAddress& address) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%u.%u.%u.%u",
        static_cast<unsigned int>(address.octets[0]),
        static_cast<unsigned int>(address.octets[1]),
        static_cast<unsigned int>(address.octets[2]),
        static_cast<unsigned int>(address.octets[3])
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        writeJsonString(response, text);
}

bool writeNativeLocalTime(WebResponseWriter& response, const LocalTime& value) {
    if (!value.valid) return writeJsonString(response, "invalid");
    char text[24] {};
    const int length = std::snprintf(
        text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned int>(value.year),
        static_cast<unsigned int>(value.month),
        static_cast<unsigned int>(value.day),
        static_cast<unsigned int>(value.hour),
        static_cast<unsigned int>(value.minute),
        static_cast<unsigned int>(value.second)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        writeJsonString(response, text);
}

void writeOutcomeUnknown(WebResponseWriter& response) {
    response.beginResponse(202U, ContentType::Json);
    response.writeText("{\"ok\":false,\"error\":\"outcome_unknown\"}");
    response.endResponse();
}

} // namespace

LumaWebApplication::LumaWebApplication(
    FirmwareApp& app,
    const AquaCore::Network::NetworkService& network,
    const AquaCore::Diagnostics::DiagnosticsService& diagnostics,
    PublishedSnapshot<LumaStatusProjection>& statusTarget,
    LumaApplicationBridge& bridge
) : app_(app), network_(network), diagnostics_(diagnostics),
    statusTarget_(statusTarget), bridge_(bridge) {}

bool LumaWebApplication::processOne(uint32_t nowMs) {
    executionNowMs_ = nowMs;
    return bridge_.processOne(execute, this);
}

FirmwareCommandResult LumaWebApplication::execute(
    const LumaWebRequest& request, void* context
) {
    LumaWebApplication* self = static_cast<LumaWebApplication*>(context);
    if (self == nullptr) return FirmwareCommandResult::Rejected;
    switch (request.kind) {
        case LumaWebRequestKind::SetMode:
            return self->app_.commandMode(request.mode);
        case LumaWebRequestKind::ExitManual:
            return self->app_.exitManual();
        case LumaWebRequestKind::SetManual:
            return self->app_.commandManual(
                request.levels, request.timeoutMinutes, self->executionNowMs_
            );
        case LumaWebRequestKind::SelectProfile:
            return self->app_.setActiveProfileIndex(request.profileIndex);
    }
    return FirmwareCommandResult::Invalid;
}

bool LumaWebApplication::publishStatus() {
    if (!app_.isRunning()) {
        return statusTarget_.invalidate();
    }
    const RuntimeState& state = app_.state();
    const DeviceConfig& config = app_.config();
    if (config.activeProfileIndex >= PROFILE_COUNT) {
        return statusTarget_.invalidate();
    }
    LumaStatusProjection value {};
    value.mode = state.mode;
    value.activeProfile = static_cast<uint8_t>(config.activeProfileIndex + 1U);
    std::strncpy(
        value.activeProfileName,
        config.profiles[config.activeProfileIndex].name,
        sizeof(value.activeProfileName) - 1U
    );
    value.dayState = state.dayState;
    value.localTime = app_.localTime();
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        value.requestedLevels[channel] = state.requestedLevels.value[channel];
        value.finalLevels[channel] = state.actualLevels.value[channel];
    }
    value.globalPowerLimit = config.globalPowerLimitPercent;
    value.timeValid = state.timeValid;
    value.wifiState = network_.state();
    value.wifiConnected = network_.isConnected();
    value.wifiIp = network_.ipAddress();
    value.wifiRssi = network_.rssi();
    value.overallHealth = diagnostics_.snapshot().overallHealth;
    return statusTarget_.publish(value);
}

LumaNativeWeb::LumaNativeWeb(
    NativeWebService& web,
    const PublishedSnapshot<LumaStatusProjection>& status,
    LumaApplicationBridge& bridge
) : web_(web), status_(status), bridge_(bridge) {}

LumaNativeWeb::~LumaNativeWeb() {
    // Stop before the route context and owned pages are destroyed.
    web_.stop();
}

bool LumaNativeWeb::registerRoutes() {
    if (registered_) return true;
    // Luma's three POST contracts stay at 512 bytes even though the shared
    // transport storage is larger for Hydro settings.
    const HttpRouteOptions body512 {512U};
    if (!web_.addPage(dashboardPage_) ||
        !web_.addPage(controlPage_) ||
        !web_.addPage(diagnosticsPage_) ||
        !web_.addPage(systemPage_) ||
        !web_.addRoute("/api/lumasense/status", HttpMethod::Get,
                       handleStatus, this) ||
        !web_.addRoute("/api/lumasense/mode", HttpMethod::Post,
                       handleMode, this, body512) ||
        !web_.addRoute("/api/lumasense/profile", HttpMethod::Post,
                       handleProfile, this, body512) ||
        !web_.addRoute("/api/lumasense/manual", HttpMethod::Post,
                       handleManual, this, body512)) {
        return false;
    }
    registered_ = true;
    return true;
}

void LumaNativeWeb::handleStatus(
    void* context, const HttpRouteRequest&, WebResponseWriter& response
) {
    const LumaNativeWeb* self = static_cast<const LumaNativeWeb*>(context);
    LumaStatusProjection value {};
    if (self == nullptr || !self->status_.read(value)) {
        writeLumaError(response, 503U, "status unavailable");
        return;
    }
    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"mode\":");
    writeJsonString(response, nativeModeName(value.mode));
    response.writeText(",\"activeProfile\":");
    writeNativeUnsigned(response, value.activeProfile);
    response.writeText(",\"activeProfileName\":");
    writeJsonString(response, value.activeProfileName);
    response.writeText(",\"dayState\":");
    writeJsonString(response, value.dayState == DayState::Night ? "NIGHT" : "DAY");
    response.writeText(",\"localTime\":");
    writeNativeLocalTime(response, value.localTime);
    response.writeText(",\"requestedLevels\":[");
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (channel != 0U) response.writeText(",");
        writeNativeFloat(response, value.requestedLevels[channel]);
    }
    response.writeText("],\"finalLevels\":[");
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (channel != 0U) response.writeText(",");
        writeNativeFloat(response, value.finalLevels[channel]);
    }
    response.writeText("],\"globalPowerLimit\":");
    writeNativeFloat(response, value.globalPowerLimit);
    response.writeText(",\"timeValid\":");
    response.writeText(value.timeValid ? "true" : "false");
    response.writeText(",\"wifi\":{\"state\":");
    writeJsonString(response, nativeNetworkStateName(value.wifiState));
    response.writeText(",\"connected\":");
    response.writeText(value.wifiConnected ? "true" : "false");
    response.writeText(",\"ip\":");
    writeNativeIp(response, value.wifiIp);
    response.writeText(",\"rssi\":");
    writeNativeSigned(response, value.wifiRssi);
    response.writeText("},\"overallHealth\":");
    writeJsonString(response, nativeHealthName(value.overallHealth));
    response.writeText("}");
    response.endResponse();
}

void LumaNativeWeb::handleMode(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
) {
    static_cast<LumaNativeWeb*>(context)->handleAction(
        request, response, parseModeRequest
    );
}

void LumaNativeWeb::handleProfile(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
) {
    static_cast<LumaNativeWeb*>(context)->handleAction(
        request, response, parseProfileRequest
    );
}

void LumaNativeWeb::handleManual(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
) {
    static_cast<LumaNativeWeb*>(context)->handleAction(
        request, response, parseManualRequest
    );
}

void LumaNativeWeb::handleAction(
    const HttpRouteRequest& request,
    WebResponseWriter& response,
    Parser parser
) {
    LumaWebRequest typed {};
    const char* error = nullptr;
    if (!parser(request.body, request.bodyLength, typed, error)) {
        writeLumaError(response, 400U, error);
        return;
    }
    ApplicationBridgeToken token {};
    const ApplicationBridgeSubmitResult submitted = bridge_.submit(typed, token);
    if (submitted != ApplicationBridgeSubmitResult::Accepted) {
        writeLumaError(response, 503U, "service unavailable");
        return;
    }
    FirmwareCommandResult result = FirmwareCommandResult::Rejected;
    const ApplicationBridgeWaitResult waited = bridge_.wait(
        token, ACTION_WAIT_TIMEOUT_MS, result
    );
    if (waited == ApplicationBridgeWaitResult::Completed) {
        writeFirmwareCommandResult(response, result);
        return;
    }
    if (waited != ApplicationBridgeWaitResult::TimedOutAccepted) {
        ApplicationBridgeAbandonResult cleanup = bridge_.abandon(token);
        if (cleanup == ApplicationBridgeAbandonResult::SynchronizationFailure) {
            (void)bridge_.abandon(token);
        }
    }
    writeOutcomeUnknown(response);
}

} // namespace Web
} // namespace LumaSense
