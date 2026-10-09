#include "AquaCore/Web/NativeWebService.h"

#include <cstdio>
#include <cstring>

#include "AquaCore/System/RestartReason.h"
#include "AquaCore/Web/HtmlShell.h"

namespace AquaCore {
namespace Web {
namespace {

bool writeBoolean(WebResponseWriter& response, bool value) {
    return response.writeText(value ? "true" : "false");
}

bool writeUnsigned(WebResponseWriter& response, uint32_t value) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%lu", static_cast<unsigned long>(value)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

bool writeSigned(WebResponseWriter& response, int32_t value) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%ld", static_cast<long>(value)
    );
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

const char* healthName(Diagnostics::HealthState state) {
    switch (state) {
        case Diagnostics::HealthState::Ok: return "ok";
        case Diagnostics::HealthState::Unknown: return "unknown";
        case Diagnostics::HealthState::Warning: return "warning";
        case Diagnostics::HealthState::Error: return "error";
    }
    return "unknown";
}

const char* timeStateName(Diagnostics::TimeState state) {
    switch (state) {
        case Diagnostics::TimeState::Unavailable: return "unavailable";
        case Diagnostics::TimeState::Invalid: return "invalid";
        case Diagnostics::TimeState::Valid: return "valid";
    }
    return "unavailable";
}

const char* storageSlotName(Config::StorageSlot slot) {
    switch (slot) {
        case Config::StorageSlot::None: return "none";
        case Config::StorageSlot::A: return "a";
        case Config::StorageSlot::B: return "b";
    }
    return "none";
}

const char* storageResultName(Config::StorageOperationResult result) {
    switch (result) {
        case Config::StorageOperationResult::NotAttempted: return "not_attempted";
        case Config::StorageOperationResult::Success: return "success";
        case Config::StorageOperationResult::Failure: return "failure";
        case Config::StorageOperationResult::NoChange: return "no_change";
        case Config::StorageOperationResult::InvalidArgument: return "invalid_argument";
        case Config::StorageOperationResult::ValidationFailure: return "validation_failure";
        case Config::StorageOperationResult::BackendFailure: return "backend_failure";
        case Config::StorageOperationResult::VerifyFailure: return "verify_failure";
    }
    return "not_attempted";
}

const char* networkStateName(Network::NetworkState state) {
    switch (state) {
        case Network::NetworkState::Disabled: return "disabled";
        case Network::NetworkState::Idle: return "idle";
        case Network::NetworkState::Connecting: return "connecting";
        case Network::NetworkState::Connected: return "connected";
        case Network::NetworkState::Disconnected: return "disconnected";
        case Network::NetworkState::Error: return "error";
    }
    return "disabled";
}

bool writeIpAddress(WebResponseWriter& response, const Network::IpAddress& address) {
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

void unavailable(WebResponseWriter& response, const char* value) {
    response.beginResponse(503U, ContentType::Json);
    response.writeText("{\"error\":");
    writeJsonString(response, value);
    response.writeText("}");
    response.endResponse();
}

} // namespace

NativeWebService::NativeWebService(
    HttpServerTransport& transport,
    const PublishedSnapshot<CoreSystemProjection>& systemSnapshot,
    const PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsSnapshot
) : transport_(transport),
    systemSnapshot_(systemSnapshot),
    diagnosticsSnapshot_(diagnosticsSnapshot) {}

NativeWebService::~NativeWebService() {
    stop();
}

bool NativeWebService::addRoute(
    const char* path, HttpMethod method,
    HttpRouteHandler handler, void* context
) {
    return addRoute(path, method, handler, context, HttpRouteOptions {});
}

bool NativeWebService::addRoute(
    const char* path, HttpMethod method,
    HttpRouteHandler handler, void* context,
    const HttpRouteOptions& options
) {
    if (registrationClosed_ || routeRegistrationFailed_ || path == nullptr ||
        std::strcmp(path, "/") == 0 ||
        std::strcmp(path, "/assets/aqua.css") == 0 ||
        std::strcmp(path, "/api/system") == 0 ||
        std::strcmp(path, "/api/diagnostics") == 0) {
        return false;
    }
    if (!transport_.addRoute(path, method, handler, context, options)) {
        // The transport contract has no rollback. After any failed delegated
        // registration this service cannot prove the table is composable.
        routeRegistrationFailed_ = true;
        return false;
    }
    return true;
}

bool NativeWebService::addPage(WebPageProvider& provider) {
    const char* path = provider.route();
    if (registrationClosed_ || routeRegistrationFailed_ || path == nullptr) {
        return false;
    }
    if (std::strcmp(path, "/") == 0) {
        if (rootPage_ != nullptr) return false;
        rootPage_ = &provider;
        return true;
    }
    if (pageRouteCount_ == MAX_PAGE_PROVIDERS ||
        std::strcmp(path, "/assets/aqua.css") == 0 ||
        std::strcmp(path, "/api/system") == 0 ||
        std::strcmp(path, "/api/diagnostics") == 0) {
        return false;
    }
    PageRoute& route = pageRoutes_[pageRouteCount_];
    route.service = this;
    route.provider = &provider;
    if (!transport_.addRoute(path, HttpMethod::Get, handlePage, &route)) {
        route = {};
        routeRegistrationFailed_ = true;
        return false;
    }
    ++pageRouteCount_;
    return true;
}

bool NativeWebService::begin(const WebConfig& config) {
    registrationClosed_ = true;
    if ((config.navigationMask & ~ALL_NAVIGATION_SECTIONS) != 0U ||
        (config.enabled && config.port == 0U)) {
        transport_.stop();
        state_ = NativeWebState::Failed;
        return false;
    }
    // A cached Running state may coexist with a transport that has stopped
    // accepting. Always let the concrete owner clean up that stale handle
    // before an explicit restart attempt.
    if (state_ == NativeWebState::Running) {
        stop();
    }
    config_ = config;
    if (!config.enabled) {
        state_ = NativeWebState::Disabled;
        return true;
    }
    if (!routesRegistered_) {
        if (routeRegistrationFailed_ || !registerRoutes()) {
            routeRegistrationFailed_ = true;
            state_ = NativeWebState::Failed;
            return false;
        }
    }
    if (!transport_.begin(config.port) || !transport_.isRunning()) {
        transport_.stop();
        state_ = NativeWebState::Failed;
        return false;
    }
    state_ = NativeWebState::Running;
    return true;
}

void NativeWebService::stop() {
    transport_.stop();
    state_ = config_.enabled
        ? NativeWebState::Stopped
        : NativeWebState::Disabled;
}

bool NativeWebService::isRunning() const {
    return state_ == NativeWebState::Running && transport_.isRunning();
}

bool NativeWebService::registerRoutes() {
    if (!transport_.addRoute("/", HttpMethod::Get, handleRoot, this) ||
        !transport_.addRoute("/assets/aqua.css", HttpMethod::Get,
                             handleStylesheet, this) ||
        !transport_.addRoute("/api/system", HttpMethod::Get,
                             handleSystem, this) ||
        !transport_.addRoute("/api/diagnostics", HttpMethod::Get,
                             handleDiagnostics, this) ||
        !transport_.setNotFoundHandler(handleNotFound, this)) {
        // The narrow transport has no rollback operation. Any partial failure
        // is therefore terminal for this service/transport instance; begin()
        // must not retry registration and collide with accepted routes.
        return false;
    }
    routesRegistered_ = true;
    return true;
}

void NativeWebService::handleRoot(
    void* context, const HttpRouteRequest&, WebResponseWriter& response
) {
    const NativeWebService* self = static_cast<const NativeWebService*>(context);
    CoreSystemProjection value {};
    if (self == nullptr || !self->systemSnapshot_.read(value)) {
        unavailable(response, "system unavailable");
        return;
    }
    WebShellInfo info {};
    info.identity = value.identity;
    std::strncpy(
        info.aquaCoreVersion,
        value.aquaCoreVersion,
        sizeof(info.aquaCoreVersion) - 1U
    );
    info.ready = value.ready;
    HtmlShell::render(
        response,
        info,
        self->config_,
        self->rootPage_ != nullptr ? self->rootPage_->title() : "Dashboard",
        self->rootPage_
    );
}

void NativeWebService::handlePage(
    void* context, const HttpRouteRequest&, WebResponseWriter& response
) {
    const PageRoute* route = static_cast<const PageRoute*>(context);
    CoreSystemProjection value {};
    if (route == nullptr || route->service == nullptr ||
        route->provider == nullptr ||
        !route->service->systemSnapshot_.read(value)) {
        unavailable(response, "system unavailable");
        return;
    }
    WebShellInfo info {};
    info.identity = value.identity;
    std::strncpy(
        info.aquaCoreVersion,
        value.aquaCoreVersion,
        sizeof(info.aquaCoreVersion) - 1U
    );
    info.ready = value.ready;
    HtmlShell::render(
        response,
        info,
        route->service->config_,
        route->provider->title(),
        route->provider
    );
}

void NativeWebService::handleStylesheet(
    void*, const HttpRouteRequest&, WebResponseWriter& response
) {
    response.beginResponse(200U, ContentType::Css);
    response.write(HtmlShell::stylesheet(), HtmlShell::stylesheetLength());
    response.endResponse();
}

void NativeWebService::handleSystem(
    void* context, const HttpRouteRequest&, WebResponseWriter& response
) {
    const NativeWebService* self = static_cast<const NativeWebService*>(context);
    CoreSystemProjection value {};
    if (self == nullptr || !self->systemSnapshot_.read(value) ||
        !value.canonicalIdentity.isValid() ||
        value.apiProtocolVersion.major == 0U) {
        unavailable(response, "system unavailable");
        return;
    }
    char deviceId[Identity::DeviceId::TEXT_LENGTH + 1U] {};
    if (!value.canonicalIdentity.deviceId().format(deviceId, sizeof(deviceId))) {
        unavailable(response, "system unavailable");
        return;
    }
    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"deviceType\":");
    writeJsonString(response, value.identity.deviceType);
    response.writeText(",\"deviceName\":");
    writeJsonString(response, value.identity.deviceName);
    response.writeText(",\"firmwareVersion\":");
    writeJsonString(response, value.identity.firmwareVersion);
    response.writeText(",\"hardwareVariant\":");
    writeJsonString(response, value.identity.hardwareVariant);
    response.writeText(",\"aquaCoreVersion\":");
    writeJsonString(response, value.aquaCoreVersion);
    response.writeText(",\"uptimeMs\":");
    writeUnsigned(response, value.uptimeMs);
    response.writeText(",\"restartReason\":");
    writeJsonString(response, restartReasonName(value.restartReason));
    response.writeText(",\"device_type\":");
    writeJsonString(response, value.canonicalIdentity.deviceType());
    response.writeText(",\"device_id\":");
    writeJsonString(response, deviceId);
    response.writeText(",\"api_protocol_version\":{\"major\":");
    writeUnsigned(response, value.apiProtocolVersion.major);
    response.writeText(",\"minor\":");
    writeUnsigned(response, value.apiProtocolVersion.minor);
    response.writeText("}");
    response.writeText("}");
    response.endResponse();
}

void NativeWebService::handleDiagnostics(
    void* context, const HttpRouteRequest&, WebResponseWriter& response
) {
    const NativeWebService* self = static_cast<const NativeWebService*>(context);
    CoreDiagnosticsProjection projection {};
    if (self == nullptr || !self->diagnosticsSnapshot_.read(projection)) {
        unavailable(response, "diagnostics unavailable");
        return;
    }
    const Diagnostics::DiagnosticsSnapshot& value = projection.value;
    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"health\":");
    writeJsonString(response, healthName(value.overallHealth));
    response.writeText(",\"system\":{\"health\":");
    writeJsonString(response, healthName(value.systemHealth));
    response.writeText(",\"ready\":");
    writeBoolean(response, value.system.ready);
    response.writeText("},\"time\":{\"health\":");
    writeJsonString(response, healthName(value.timeHealth));
    response.writeText(",\"state\":");
    writeJsonString(response, timeStateName(value.time.state));
    response.writeText(",\"rtcReady\":");
    writeBoolean(response, value.time.rtcReady);
    response.writeText(",\"rtcValid\":");
    writeBoolean(response, value.time.rtcValid);
    response.writeText(",\"provider\":");
    writeJsonString(response, value.time.providerName);
    response.writeText("},\"storage\":{\"health\":");
    writeJsonString(response, healthName(value.storageHealth));
    response.writeText(",\"backendReady\":");
    writeBoolean(response, value.storage.backendReady);
    response.writeText(",\"hasValidPayload\":");
    writeBoolean(response, value.storage.hasValidPayload);
    response.writeText(",\"activeSlot\":");
    writeJsonString(response, storageSlotName(value.storage.activeSlot));
    response.writeText(",\"generation\":");
    writeUnsigned(response, value.storage.activeGeneration);
    response.writeText(",\"lastLoad\":");
    writeJsonString(response, storageResultName(value.storage.lastLoadResult));
    response.writeText(",\"lastSave\":");
    writeJsonString(response, storageResultName(value.storage.lastSaveResult));
    response.writeText("},\"network\":{\"health\":");
    writeJsonString(response, healthName(value.networkHealth));
    response.writeText(",\"available\":");
    writeBoolean(response, value.network.available);
    response.writeText(",\"state\":");
    writeJsonString(response, networkStateName(value.network.state));
    response.writeText(",\"connected\":");
    writeBoolean(response, value.network.connected);
    response.writeText(",\"ssid\":");
    writeJsonString(response, value.network.ssid);
    response.writeText(",\"hostname\":");
    writeJsonString(response, value.network.hostname);
    response.writeText(",\"ipAddress\":");
    writeIpAddress(response, value.network.ipAddress);
    response.writeText(",\"rssi\":");
    writeSigned(response, value.network.rssi);
    response.writeText(",\"reconnectCount\":");
    writeUnsigned(response, value.network.reconnectCount);
    response.writeText(",\"apActive\":");
    writeBoolean(response, value.network.apActive);
    response.writeText(",\"apSsid\":");
    writeJsonString(response, value.network.apSsid);
    response.writeText(",\"apIpAddress\":");
    writeIpAddress(response, value.network.apIpAddress);
    response.writeText("}}");
    response.endResponse();
}

void NativeWebService::handleNotFound(void*, WebResponseWriter& response) {
    response.beginResponse(404U, ContentType::PlainText);
    response.writeText("Not Found");
    response.endResponse();
}

} // namespace Web
} // namespace AquaCore
