#include "AquaCore/Web/WebService.h"

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

bool writeUnsigned(
    WebResponseWriter& response,
    uint32_t value
) {
    char text[16] {};
    const int length = std::snprintf(
        text,
        sizeof(text),
        "%lu",
        static_cast<unsigned long>(value)
    );
    return length > 0 &&
        static_cast<size_t>(length) < sizeof(text) &&
        response.write(text, static_cast<size_t>(length));
}

bool writeSigned(
    WebResponseWriter& response,
    int32_t value
) {
    char text[16] {};
    const int length = std::snprintf(
        text,
        sizeof(text),
        "%ld",
        static_cast<long>(value)
    );
    return length > 0 &&
        static_cast<size_t>(length) < sizeof(text) &&
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

const char* storageResultName(
    Config::StorageOperationResult result
) {
    switch (result) {
        case Config::StorageOperationResult::NotAttempted:
            return "not_attempted";
        case Config::StorageOperationResult::Success:
            return "success";
        case Config::StorageOperationResult::Failure:
            return "failure";
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

bool writeIpAddress(
    WebResponseWriter& response,
    const Network::IpAddress& address
) {
    char text[16] {};
    const int length = std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u",
        static_cast<unsigned int>(address.octets[0]),
        static_cast<unsigned int>(address.octets[1]),
        static_cast<unsigned int>(address.octets[2]),
        static_cast<unsigned int>(address.octets[3])
    );
    return length > 0 &&
        static_cast<size_t>(length) < sizeof(text) &&
        writeJsonString(response, text);
}

void unavailableJson(
    WebResponseWriter& response,
    const char* service
) {
    response.beginResponse(503U, ContentType::Json);
    response.writeText("{\"error\":");
    writeJsonString(response, service);
    response.writeText("}");
    response.endResponse();
}

} // namespace

WebService::WebService(WebBackend& backend)
    : backend_(backend),
      system_(nullptr),
      diagnostics_(nullptr) {
}

WebService::WebService(
    WebBackend& backend,
    const SystemService& system,
    const Diagnostics::DiagnosticsService* diagnostics
)
    : backend_(backend),
      system_(&system),
      diagnostics_(diagnostics) {
}

bool WebService::begin(const WebConfig& config) {
    if (
        (config.navigationMask & ~ALL_NAVIGATION_SECTIONS) != 0U ||
        (config.enabled && config.port == 0U)
    ) {
        return false;
    }

    if (running_) {
        stop();
    }

    config_ = config;
    if (!config_.enabled) {
        return true;
    }

    if (!registerDefaultRoutes()) {
        return false;
    }

    running_ = backend_.begin(config_.port);
    return running_;
}

void WebService::update() {
    if (isRunning()) {
        backend_.update();
    }
}

void WebService::stop() {
    if (running_ || backend_.isRunning()) {
        backend_.stop();
    }
    running_ = false;
}

bool WebService::isRunning() const {
    return running_ && backend_.isRunning();
}

bool WebService::addRoute(
    const char* path,
    HttpMethod method,
    WebRouteHandler handler,
    void* context
) {
    return addRoute(
        path,
        method,
        handler,
        context,
        WebRouteOptions {}
    );
}

bool WebService::addRoute(
    const char* path,
    HttpMethod method,
    WebRouteHandler handler,
    void* context,
    const WebRouteOptions& options
) {
    if (!isValidPath(path) || handler == nullptr) {
        return false;
    }

    if (
        (options.uploadHandler != nullptr && method != HttpMethod::Post) ||
        (options.uploadHandler == nullptr && options.uploadContext != nullptr)
    ) {
        return false;
    }

    return backend_.addRoute(
        path,
        method,
        handler,
        context,
        options
    );
}

bool WebService::addPage(WebPageProvider& provider) {
    if (
        pageCount_ >= MAX_PAGE_PROVIDERS ||
        !isValidPath(provider.route()) ||
        provider.title() == nullptr
    ) {
        return false;
    }

    if (std::strcmp(provider.route(), "/") == 0) {
        if (rootPage_ != nullptr) {
            return false;
        }
        rootPage_ = &provider;
        return true;
    }

    PageRegistration& registration = pages_[pageCount_];
    registration.service = this;
    registration.provider = &provider;

    if (!backend_.addRoute(
        provider.route(),
        HttpMethod::Get,
        handlePage,
        &registration
    )) {
        registration = PageRegistration {};
        return false;
    }

    ++pageCount_;
    return true;
}

bool WebService::addApi(WebApiProvider& provider) {
    if (
        apiCount_ >= MAX_API_PROVIDERS ||
        !isValidPath(provider.route())
    ) {
        return false;
    }

    ApiRegistration& registration = apis_[apiCount_];
    registration.provider = &provider;

    if (!backend_.addRoute(
        provider.route(),
        provider.method(),
        handleApi,
        &registration
    )) {
        registration = ApiRegistration {};
        return false;
    }

    ++apiCount_;
    return true;
}

bool WebService::registerDefaultRoutes() {
    while (defaultRegistrationStep_ < 5U) {
        bool registered = false;
        switch (defaultRegistrationStep_) {
            case 0U:
                registered = backend_.addRoute(
                    "/",
                    HttpMethod::Get,
                    handleRoot,
                    this
                );
                break;
            case 1U:
                registered = backend_.addRoute(
                    "/assets/aqua.css",
                    HttpMethod::Get,
                    handleStylesheet,
                    this
                );
                break;
            case 2U:
                registered = backend_.addRoute(
                    "/api/system",
                    HttpMethod::Get,
                    handleSystemApi,
                    this
                );
                break;
            case 3U:
                registered = backend_.addRoute(
                    "/api/diagnostics",
                    HttpMethod::Get,
                    handleDiagnosticsApi,
                    this
                );
                break;
            case 4U:
                registered = backend_.setNotFoundHandler(
                    handleNotFound,
                    this
                );
                break;
            default:
                return false;
        }

        if (!registered) {
            return false;
        }
        ++defaultRegistrationStep_;
    }

    return true;
}

bool WebService::isValidPath(const char* path) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }

    size_t length = 0U;
    while (path[length] != '\0') {
        if (
            path[length] == ' ' ||
            path[length] == '\r' ||
            path[length] == '\n' ||
            ++length >= 64U
        ) {
            return false;
        }
    }
    return length > 0U;
}

void WebService::handleRoot(
    void* context,
    const WebRequest&,
    WebResponseWriter& response
) {
    WebService* service = static_cast<WebService*>(context);
    if (service == nullptr) {
        unavailableJson(response, "web service unavailable");
        return;
    }

    HtmlShell::render(
        response,
        service->system_,
        service->config_,
        "Dashboard",
        service->rootPage_
    );
}

void WebService::handleStylesheet(
    void*,
    const WebRequest&,
    WebResponseWriter& response
) {
    response.beginResponse(200U, ContentType::Css);
    response.write(
        HtmlShell::stylesheet(),
        HtmlShell::stylesheetLength()
    );
    response.endResponse();
}

void WebService::handleSystemApi(
    void* context,
    const WebRequest&,
    WebResponseWriter& response
) {
    WebService* service = static_cast<WebService*>(context);
    if (service == nullptr || service->system_ == nullptr) {
        unavailableJson(response, "system unavailable");
        return;
    }

    const SystemService& system = *service->system_;
    const DeviceIdentity& identity = system.deviceIdentity();

    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"deviceType\":");
    writeJsonString(response, identity.deviceType);
    response.writeText(",\"deviceName\":");
    writeJsonString(response, identity.deviceName);
    response.writeText(",\"firmwareVersion\":");
    writeJsonString(response, identity.firmwareVersion);
    response.writeText(",\"hardwareVariant\":");
    writeJsonString(response, identity.hardwareVariant);
    response.writeText(",\"aquaCoreVersion\":");
    writeJsonString(response, system.aquaCoreVersion());
    response.writeText(",\"uptimeMs\":");
    writeUnsigned(response, system.uptimeMs());
    response.writeText(",\"restartReason\":");
    writeJsonString(
        response,
        restartReasonName(system.restartReason())
    );
    response.writeText("}");
    response.endResponse();
}

void WebService::handleDiagnosticsApi(
    void* context,
    const WebRequest&,
    WebResponseWriter& response
) {
    WebService* service = static_cast<WebService*>(context);
    if (
        service == nullptr ||
        service->diagnostics_ == nullptr
    ) {
        unavailableJson(response, "diagnostics unavailable");
        return;
    }

    const Diagnostics::DiagnosticsSnapshot value =
        service->diagnostics_->snapshot();

    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"health\":");
    writeJsonString(response, healthName(value.overallHealth));

    response.writeText(",\"system\":{\"health\":");
    writeJsonString(response, healthName(value.systemHealth));
    response.writeText(",\"ready\":");
    writeBoolean(response, value.system.ready);
    response.writeText("}");

    response.writeText(",\"time\":{\"health\":");
    writeJsonString(response, healthName(value.timeHealth));
    response.writeText(",\"state\":");
    writeJsonString(response, timeStateName(value.time.state));
    response.writeText(",\"rtcReady\":");
    writeBoolean(response, value.time.rtcReady);
    response.writeText(",\"rtcValid\":");
    writeBoolean(response, value.time.rtcValid);
    response.writeText(",\"provider\":");
    writeJsonString(response, value.time.providerName);
    response.writeText("}");

    response.writeText(",\"storage\":{\"health\":");
    writeJsonString(response, healthName(value.storageHealth));
    response.writeText(",\"backendReady\":");
    writeBoolean(response, value.storage.backendReady);
    response.writeText(",\"hasValidPayload\":");
    writeBoolean(response, value.storage.hasValidPayload);
    response.writeText(",\"activeSlot\":");
    writeJsonString(
        response,
        storageSlotName(value.storage.activeSlot)
    );
    response.writeText(",\"generation\":");
    writeUnsigned(response, value.storage.activeGeneration);
    response.writeText(",\"lastLoad\":");
    writeJsonString(
        response,
        storageResultName(value.storage.lastLoadResult)
    );
    response.writeText(",\"lastSave\":");
    writeJsonString(
        response,
        storageResultName(value.storage.lastSaveResult)
    );
    response.writeText("}");

    response.writeText(",\"network\":{\"health\":");
    writeJsonString(response, healthName(value.networkHealth));
    response.writeText(",\"available\":");
    writeBoolean(response, value.network.available);
    response.writeText(",\"state\":");
    writeJsonString(
        response,
        networkStateName(value.network.state)
    );
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

void WebService::handleNotFound(
    void*,
    const WebRequest&,
    WebResponseWriter& response
) {
    response.beginResponse(404U, ContentType::PlainText);
    response.writeText("Not Found");
    response.endResponse();
}

void WebService::handlePage(
    void* context,
    const WebRequest&,
    WebResponseWriter& response
) {
    PageRegistration* registration =
        static_cast<PageRegistration*>(context);
    if (
        registration == nullptr ||
        registration->service == nullptr ||
        registration->provider == nullptr
    ) {
        unavailableJson(response, "page unavailable");
        return;
    }

    HtmlShell::render(
        response,
        registration->service->system_,
        registration->service->config_,
        registration->provider->title(),
        registration->provider
    );
}

void WebService::handleApi(
    void* context,
    const WebRequest& request,
    WebResponseWriter& response
) {
    ApiRegistration* registration =
        static_cast<ApiRegistration*>(context);
    if (
        registration == nullptr ||
        registration->provider == nullptr
    ) {
        unavailableJson(response, "api unavailable");
        return;
    }

    registration->provider->handle(request, response);
}

} // namespace Web
} // namespace AquaCore
