#include "AquaCore/Web/EspIdfWebTransport.h"
#include "AquaCore/Web/HttpBasicAuth.h"
#include "AquaCore/Web/RealtimeStreamStartWire.h"
#include "HttpCoreReservedPaths.h"
#include "HttpServerStopLifecycle.h"
#include "HttpStreamingReceive.h"

#include <cstdlib>
#include <limits.h>
#include <string.h>

namespace AquaCore {
namespace Web {
namespace {

constexpr size_t HTTP_OVERSIZE_DRAIN_LIMIT = HTTP_NORMAL_BODY_CAPACITY + 1U;
constexpr size_t OVERSIZE_DISCARD_CHUNK_SIZE = 64U;

const char* statusLine(uint16_t code) {
    switch (code) {
        case 200U: return "200 OK";
        case 201U: return "201 Created";
        case 202U: return "202 Accepted";
        case 204U: return "204 No Content";
        case 400U: return "400 Bad Request";
        case 401U: return "401 Unauthorized";
        case 403U: return "403 Forbidden";
        case 404U: return "404 Not Found";
        case 405U: return "405 Method Not Allowed";
        case 408U: return "408 Request Timeout";
        case 409U: return "409 Conflict";
        case 413U: return "413 Payload Too Large";
        case 422U: return "422 Unprocessable Entity";
        case 429U: return "429 Too Many Requests";
        case 500U: return "500 Internal Server Error";
        case 503U: return "503 Service Unavailable";
        default: return nullptr;
    }
}

class IdfResponseWriter final : public WebResponseWriter {
public:
    explicit IdfResponseWriter(httpd_req_t& request) : request_(request) {}

    bool beginResponse(uint16_t code, ContentType type) override {
        const char* status = statusLine(code);
        if (started_ || status == nullptr ||
            httpd_resp_set_status(&request_, status) != ESP_OK ||
            httpd_resp_set_type(&request_, contentTypeName(type)) != ESP_OK) {
            return false;
        }
        started_ = true;
        return true;
    }

    bool write(const char* data, size_t length) override {
        if (!started_ || ended_ || failed_ ||
            (data == nullptr && length != 0U) ||
            length > static_cast<size_t>(INT_MAX)) {
            return false;
        }
        if (length == 0U) {
            return true;
        }
        const esp_err_t result = httpd_resp_send_chunk(
            &request_, data, static_cast<ssize_t>(length)
        );
        failed_ = result != ESP_OK;
        return !failed_;
    }

    bool endResponse() override {
        if (!started_ || ended_ || failed_) {
            return false;
        }
        if (httpd_resp_send_chunk(&request_, nullptr, 0) != ESP_OK) {
            failed_ = true;
            return false;
        }
        ended_ = true;
        return true;
    }

    esp_err_t finish() {
        if (!started_) {
            return httpd_resp_send_500(&request_);
        }
        return ended_ && !failed_ ? ESP_OK : ESP_FAIL;
    }

private:
    httpd_req_t& request_;
    bool started_ = false;
    bool ended_ = false;
    bool failed_ = false;
};

// Borrowed only for one HTTPD callback; no raw request escapes to a route.
class IdfRequestContext final : public WebRequestContext {
public:
    IdfRequestContext(httpd_req_t& request, IdfResponseWriter& response)
        : request_(request), response_(response) {}

    bool hasHeader(const char* name) const override {
        if (name == nullptr || name[0] == '\0') return false;
        if (httpd_req_get_hdr_value_len(&request_, name) != 0U) return true;
        char empty[1U] {};
        return httpd_req_get_hdr_value_str(&request_, name, empty,
                                           sizeof(empty)) == ESP_OK;
    }

    size_t copyHeader(const char* name, char* output,
                      size_t capacity) const override {
        if (output != nullptr && capacity != 0U) output[0] = '\0';
        if (name == nullptr || name[0] == '\0') return 0U;
        const size_t length = httpd_req_get_hdr_value_len(&request_, name);
        if (output == nullptr || capacity <= length) return length;
        if (length != 0U &&
            httpd_req_get_hdr_value_str(&request_, name, output, capacity) != ESP_OK)
            output[0] = '\0';
        return length;
    }

    bool authenticateBasic(const char* user,
                           const char* password) const override {
        if (user == nullptr || password == nullptr) return false;
        const size_t length = httpd_req_get_hdr_value_len(
            &request_, "Authorization");
        if (length == 0U || length > HTTP_AUTHORIZATION_HEADER_CAPACITY)
            return false;
        char header[HTTP_AUTHORIZATION_HEADER_CAPACITY + 1U] {};
        const bool copied = httpd_req_get_hdr_value_str(
            &request_, "Authorization", header, sizeof(header)) == ESP_OK;
        const bool accepted = copied &&
            verifyHttpBasicAuthorization(header, length, user, password);
        volatile char* wipe = header;
        for (size_t i = 0U; i < sizeof(header); ++i) wipe[i] = '\0';
        return accepted;
    }

    bool requestBasicAuthentication(const char* realm) const override {
        if (!validHttpBasicRealm(realm)) return false;
        const char prefix[] = "Basic realm=\"";
        char challenge[sizeof(prefix) + HTTP_BASIC_REALM_CAPACITY + 1U] {};
        const size_t realmLength = strlen(realm);
        memcpy(challenge, prefix, sizeof(prefix) - 1U);
        memcpy(challenge + sizeof(prefix) - 1U, realm, realmLength);
        challenge[sizeof(prefix) - 1U + realmLength] = '"';
        return httpd_resp_set_hdr(&request_, "WWW-Authenticate", challenge) == ESP_OK &&
               response_.beginResponse(401U, ContentType::PlainText) &&
               response_.writeText("Unauthorized") && response_.endResponse();
    }

private:
    httpd_req_t& request_;
    IdfResponseWriter& response_;
};

esp_err_t rejectUnreadBody(httpd_req_t* request, const char* status,
                           const char* message) {
    // Returning success would make HTTPD purge unread bytes before reuse.
    (void)httpd_resp_send_custom_err(request, status, message);
    return ESP_FAIL;
}

esp_err_t rejectOversizeBody(httpd_req_t* request) {
    // Only the maximum normal body plus one byte is drained. Gross oversize
    // fails closed without a response; HTTPD must not purge an unbounded body.
    if (request->content_len > HTTP_OVERSIZE_DRAIN_LIMIT) return ESP_FAIL;

    char discard[OVERSIZE_DISCARD_CHUNK_SIZE];
    size_t remaining = request->content_len;
    while (remaining != 0U) {
        const size_t chunk = remaining < sizeof(discard)
            ? remaining : sizeof(discard);
        const ssize_t received = httpd_req_recv(request, discard, chunk);
        if (received <= 0 || static_cast<size_t>(received) > chunk)
            return ESP_FAIL;
        remaining -= static_cast<size_t>(received);
    }

    // With no unread body, ESP_OK cannot trigger an unbounded HTTPD purge.
    if (httpd_resp_set_status(request, "413 Payload Too Large") != ESP_OK ||
        httpd_resp_set_hdr(request, "Connection", "close") != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send(request, nullptr, 0);
}

int streamRead(void* context, uint8_t* buffer, size_t capacity) {
    return static_cast<int>(httpd_req_recv(
        static_cast<httpd_req_t*>(context), reinterpret_cast<char*>(buffer), capacity
    ));
}

bool streamAccepting(void* context) {
    return static_cast<EspIdfWebTransport*>(context)->isRunning();
}

esp_err_t rejectOversizeStream(httpd_req_t* request) {
    // No recv: returning failure prevents HTTPD from purging the unread body.
    if (httpd_resp_set_status(request, "413 Payload Too Large") == ESP_OK &&
        httpd_resp_set_hdr(request, "Connection", "close") == ESP_OK) {
        (void)httpd_resp_send(request, nullptr, 0);
    }
    return ESP_FAIL;
}

void streamOversize(void* context) {
    (void)rejectOversizeStream(static_cast<httpd_req_t*>(context));
}

bool stopHttpd(httpd_handle_t handle, void*) {
    return httpd_stop(handle) == ESP_OK;
}

} // namespace

EspIdfWebTransport::EspIdfWebTransport()
    : realtimeMutex_(xSemaphoreCreateMutexStatic(&realtimeMutexStorage_)) {}

EspIdfWebTransport::~EspIdfWebTransport() {
    stop();
    if (realtimeMutex_ != nullptr) {
        vSemaphoreDelete(realtimeMutex_);
        realtimeMutex_ = nullptr;
    }
}

bool EspIdfWebTransport::addRoute(
    const char* path, HttpMethod method,
    HttpRouteHandler handler, void* context,
    const HttpRouteOptions& options
) {
    if (registrationFailed_ || server_ != nullptr ||
        (realtimeConfigured_ && method == HttpMethod::Get && path != nullptr &&
         strcmp(path, realtimePath_) == 0)) {
        return false;
    }
    return routes_.addRoute(path, method, handler, context, options);
}

bool EspIdfWebTransport::addStreamingRoute(
    const char* path, HttpMethod method, HttpStreamHandler handler,
    void* context, size_t maxContentLength
) {
    if (registrationFailed_ || server_ != nullptr || routes_.isFrozen())
        return false;
    if (Internal::isCoreReadPath(path)) {
        // These paths belong to the Core read surface for either method.
        // A failed attempt makes the final composition fail closed.
        registrationFailed_ = true;
        return false;
    }
    if (realtimeConfigured_ && method == HttpMethod::Get && path != nullptr &&
        strcmp(path, realtimePath_) == 0) return false;
    return routes_.addStreamingRoute(path, method, handler, context,
                                     maxContentLength);
}

bool EspIdfWebTransport::setNotFoundHandler(
    HttpNotFoundHandler handler, void* context
) {
    return !registrationFailed_ && server_ == nullptr &&
        routes_.setNotFoundHandler(handler, context);
}

bool EspIdfWebTransport::setRealtimeEndpoint(const char* path) {
    if (registrationFailed_ || server_ != nullptr || routes_.isFrozen() || realtimeConfigured_ ||
        path == nullptr || path[0] != '/') {
        return false;
    }
    size_t length = 0U;
    while (path[length] != '\0') {
        if (length == REALTIME_PATH_CAPACITY || path[length] <= ' ') {
            return false;
        }
        ++length;
    }
    if (length == 0U) {
        return false;
    }
    for (size_t i = 0U; i < routes_.size(); ++i) {
        const HttpRouteRegistry::Route* route = routes_.routeAt(i);
        if (route->method == HttpMethod::Get && strcmp(route->path, path) == 0) {
            return false;
        }
    }
    memcpy(realtimePath_, path, length + 1U);
    realtimeConfigured_ = true;
    return true;
}

RealtimePublicationResult EspIdfWebTransport::publishRealtime(
    const RealtimeNotificationMetadata& metadata, RealtimeFrameType type,
    const uint8_t* payload, size_t length
) {
    if (realtimeMutex_ == nullptr ||
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY) != pdTRUE) {
        return RealtimePublicationResult::NotRunning;
    }
    if (!isRunning() || !realtimeConfigured_) {
        xSemaphoreGive(realtimeMutex_);
        return RealtimePublicationResult::NotRunning;
    }
    if (realtimeRecovery_.isRequired()) {
        xSemaphoreGive(realtimeMutex_);
        return RealtimePublicationResult::RecoveryRequired;
    }
    if (!metadata.isValid()) {
        xSemaphoreGive(realtimeMutex_);
        return RealtimePublicationResult::InvalidMetadata;
    }
    if ((payload == nullptr && length != 0U) ||
        length > REALTIME_PAYLOAD_CAPACITY) {
        xSemaphoreGive(realtimeMutex_);
        return RealtimePublicationResult::PayloadTooLarge;
    }
    for (size_t i = 0U; i < REALTIME_WORK_CAPACITY; ++i) {
        RealtimeWork& work = realtimeWork_[i];
        bool expected = false;
        if (work.inUse.compare_exchange_strong(expected, true)) {
            work.owner = this;
            work.metadata = metadata;
            work.length = length;
            work.type = type;
            work.recoveryGeneration = realtimeRecovery_.generation();
            if (length != 0U) {
                memcpy(work.payload, payload, length);
            }
            if (httpd_queue_work(server_, realtimeWork, &work) != ESP_OK) {
                work.inUse.store(false);
                xSemaphoreGive(realtimeMutex_);
                return RealtimePublicationResult::QueueWorkFailure;
            }
            RealtimeStreamPosition position;
            RealtimeStreamPosition::at(metadata.sequence(), position);
            realtimeClients_.notePublication(position);
            xSemaphoreGive(realtimeMutex_);
            return RealtimePublicationResult::Accepted;
        }
    }
    xSemaphoreGive(realtimeMutex_);
    return RealtimePublicationResult::Busy;
}

bool EspIdfWebTransport::publishRealtimeStreamStartState(
    const RealtimeStreamStartState& state
) {
    if (!state.isAvailable() || realtimeMutex_ == nullptr ||
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    streamStartState_ = state;
    xSemaphoreGive(realtimeMutex_);
    return true;
}

void EspIdfWebTransport::requestRealtimeRecovery() {
    realtimeRecovery_.request();
    if (realtimeMutex_ == nullptr ||
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }
    realtimeClients_.requestRecovery();
    xSemaphoreGive(realtimeMutex_);
}

RealtimeRecoveryServiceResult EspIdfWebTransport::serviceRealtimeRecovery() {
    if (realtimeMutex_ == nullptr ||
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY) != pdTRUE) {
        return RealtimeRecoveryServiceResult::RetryNeeded;
    }
    httpd_handle_t server = server_;
    if (realtimeRecovery_.isRequired()) {
        // Repeat the client transition under the mutex so a recovery request
        // remains effective even if its first registry lock could not be taken.
        realtimeClients_.requestRecovery();
    }
    RealtimeClientToken token;
    int fd = -1;
    bool closeRequested = false;
    for (size_t i = 0U; i < REALTIME_CLIENT_CAPACITY; ++i) {
        RealtimeClientRegistry<REALTIME_CLIENT_CAPACITY>::Slot* slot =
            realtimeClients_.slot(i);
        if (slot == nullptr || slot->state == RealtimeClientState::Free) continue;
        const RealtimeClientToken candidate(i, slot->generation);
        if (server != nullptr &&
            httpd_ws_get_fd_info(server, slot->fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            realtimeClients_.reclaimInactive(candidate, slot->fd);
            xSemaphoreGive(realtimeMutex_);
            return RealtimeRecoveryServiceResult::Progress;
        }
        if (slot->state == RealtimeClientState::Closing) {
            token.slot = i;
            token.generation = slot->generation;
            fd = slot->fd;
            closeRequested = slot->closeRequested;
            break;
        }
    }
    xSemaphoreGive(realtimeMutex_);
    if (fd < 0) return RealtimeRecoveryServiceResult::Idle;
    if (server == nullptr) {
        return RealtimeRecoveryServiceResult::TransportRecycleSuggested;
    }
    if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        if (xSemaphoreTake(realtimeMutex_, portMAX_DELAY) == pdTRUE) {
            realtimeClients_.reclaimInactive(token, fd);
            xSemaphoreGive(realtimeMutex_);
        }
        return RealtimeRecoveryServiceResult::Progress;
    }
    if (closeRequested) {
        return RealtimeRecoveryServiceResult::RetryNeeded;
    }
    const esp_err_t closeResult = httpd_sess_trigger_close(server, fd);
    if (closeResult == ESP_ERR_NOT_FOUND) {
        if (xSemaphoreTake(realtimeMutex_, portMAX_DELAY) == pdTRUE) {
            realtimeClients_.reclaimInactive(token, fd);
            xSemaphoreGive(realtimeMutex_);
        }
        return RealtimeRecoveryServiceResult::Progress;
    }
    if (closeResult != ESP_OK) {
        return RealtimeRecoveryServiceResult::RetryNeeded;
    }
    if (xSemaphoreTake(realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        realtimeClients_.markCloseRequested(token, fd);
        xSemaphoreGive(realtimeMutex_);
    }
    return RealtimeRecoveryServiceResult::Progress;
}

bool EspIdfWebTransport::tryClearRealtimeRecovery(
    Identity::RuntimeIdentity runtime,
    RealtimeStreamPosition coherentPosition
) {
    if (!runtime.isValid() || !coherentPosition.isValid() ||
        realtimeMutex_ == nullptr ||
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const bool canClear = realtimeRecovery_.tryClear(
        !realtimeClients_.hasClients(), isRunning(), streamStartState_, runtime,
        coherentPosition
    );
    xSemaphoreGive(realtimeMutex_);
    return canClear;
}

bool EspIdfWebTransport::begin(uint16_t port) {
    if (port == 0U || registrationFailed_) {
        return false;
    }
    if (!Internal::canStartNewServer(server_)) {
        return accepting_.load() && port_ == port;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = static_cast<uint16_t>(REALTIME_CLIENT_CAPACITY);
    // 24 fixed HTTP routes plus one reserved URI slot for F9.5 WS on this
    // same handle. This is an implementation capacity, not a platform rule.
    config.max_uri_handlers = static_cast<uint16_t>(
        HttpRouteRegistry::MAX_ROUTES + 1U
    );
    config.global_user_ctx = this;
    config.global_user_ctx_free_fn = keepBorrowedContext;

    if (httpd_start(&server_, &config) != ESP_OK) {
        server_ = nullptr;
        return false;
    }
    port_ = port;

    for (size_t i = 0U; i < routes_.size(); ++i) {
        const HttpRouteRegistry::Route* route = routes_.routeAt(i);
        httpd_uri_t uri {};
        uri.uri = route->path;
        uri.method = route->method == HttpMethod::Get ? HTTP_GET : HTTP_POST;
        uri.handler = route->kind == HttpRouteRegistry::RouteKind::Streaming
            ? streamDispatch : dispatch;
        uri.user_ctx = const_cast<HttpRouteRegistry::Route*>(route);
        if (httpd_register_uri_handler(server_, &uri) != ESP_OK) {
            registrationFailed_ = true;
            routes_.freeze();
            stop();
            return false;
        }
    }

    if (realtimeConfigured_) {
        httpd_uri_t realtime {};
        realtime.uri = realtimePath_;
        realtime.method = HTTP_GET;
        realtime.handler = realtimeDispatch;
        realtime.user_ctx = this;
        realtime.is_websocket = true;
        realtime.handle_ws_control_frames = true;
        if (httpd_register_uri_handler(server_, &realtime) != ESP_OK) {
            registrationFailed_ = true;
            routes_.freeze();
            stop();
            return false;
        }
    }

    if (httpd_register_err_handler(
            server_, HTTPD_404_NOT_FOUND, notFound
        ) != ESP_OK) {
        registrationFailed_ = true;
        routes_.freeze();
        stop();
        return false;
    }

    routes_.freeze();
    accepting_.store(true);
    return true;
}

bool EspIdfWebTransport::tryStop() {
    httpd_handle_t stopping = nullptr;
    if (realtimeMutex_ != nullptr) {
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY);
    }
    accepting_.store(false);
    stopping = server_;
    server_ = nullptr;
    if (realtimeMutex_ != nullptr) {
        xSemaphoreGive(realtimeMutex_);
    }
    // httpd_stop waits for HTTPD callbacks, so it must run without the
    // lifecycle mutex held. A failed send leaves stopping and port_ intact.
    if (!Internal::stopRetainingHandle(stopping, port_, stopHttpd, nullptr)) {
        if (realtimeMutex_ != nullptr) {
            xSemaphoreTake(realtimeMutex_, portMAX_DELAY);
        }
        server_ = stopping;
        if (realtimeMutex_ != nullptr) {
            xSemaphoreGive(realtimeMutex_);
        }
        return false;
    }
    if (realtimeMutex_ != nullptr) {
        xSemaphoreTake(realtimeMutex_, portMAX_DELAY);
    }
    for (size_t i = 0U; i < REALTIME_WORK_CAPACITY; ++i) {
        realtimeWork_[i].inUse.store(false);
    }
    for (size_t i = 0U; i < REALTIME_CLIENT_CAPACITY; ++i) {
        streamStartWork_[i].inUse.store(false);
    }
    realtimeClients_.reset();
    if (realtimeMutex_ != nullptr) {
        xSemaphoreGive(realtimeMutex_);
    }
    return true;
}

void EspIdfWebTransport::stop() {
    // This is also called by NativeWebService before its own destruction.
    // Returning after an unsuccessful shutdown would let service, product,
    // route and Realtime callback contexts be destroyed under a live HTTPD.
    if (!tryStop() || Internal::destructionDisposition(server_) !=
                          Internal::DestructionDisposition::Safe)
        std::abort();
}

esp_err_t EspIdfWebTransport::realtimeDispatch(httpd_req_t* request) {
    if (request == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->method == HTTP_GET) {
        EspIdfWebTransport* owner = static_cast<EspIdfWebTransport*>(
            httpd_get_global_user_ctx(request->handle)
        );
        if (owner == nullptr || owner->realtimeMutex_ == nullptr ||
            xSemaphoreTake(owner->realtimeMutex_, portMAX_DELAY) != pdTRUE) {
            return ESP_FAIL;
        }
        const int fd = httpd_req_to_sockfd(request);
        if (!owner->accepting_.load() || owner->realtimeRecovery_.isRequired() ||
            !owner->streamStartState_.isAvailable() || fd < 0) {
            xSemaphoreGive(owner->realtimeMutex_);
            return ESP_FAIL;
        }
        // A new handshake on a reused fd invalidates only an observed stale
        // occupant. Its generation prevents queued old work from matching.
        for (size_t i = 0U; i < REALTIME_CLIENT_CAPACITY; ++i) {
            RealtimeClientRegistry<REALTIME_CLIENT_CAPACITY>::Slot* slot =
                owner->realtimeClients_.slot(i);
            if (slot != nullptr && slot->state != RealtimeClientState::Free &&
                slot->fd == fd) {
                const RealtimeClientToken stale {i, slot->generation};
                owner->realtimeClients_.release(stale);
            }
        }
        RealtimeClientToken token;
        if (!owner->realtimeClients_.connect(
                fd, owner->streamStartState_.position(), token)) {
            xSemaphoreGive(owner->realtimeMutex_);
            return ESP_FAIL;
        }
        StreamStartWork& work = owner->streamStartWork_[token.slot];
        bool expected = false;
        if (!work.inUse.compare_exchange_strong(expected, true)) {
            owner->realtimeClients_.release(token);
            xSemaphoreGive(owner->realtimeMutex_);
            return ESP_FAIL;
        }
        work.owner = owner;
        work.token = token;
        work.state = owner->streamStartState_;
        work.recoveryGeneration = owner->realtimeRecovery_.generation();
        if (!encodeRealtimeStreamStart(work.state, work.payload,
                                       sizeof(work.payload), work.length) ||
            httpd_queue_work(owner->server_, streamStartWork, &work) != ESP_OK) {
            work.inUse.store(false);
            owner->realtimeClients_.release(token);
            xSemaphoreGive(owner->realtimeMutex_);
            return ESP_FAIL;
        }
        xSemaphoreGive(owner->realtimeMutex_);
        return ESP_OK;
    }
    httpd_ws_frame_t frame {};
    if (httpd_ws_recv_frame(request, &frame, 0U) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.len > REALTIME_PAYLOAD_CAPACITY) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t ignored[REALTIME_PAYLOAD_CAPACITY] {};
    frame.payload = ignored;
    if (frame.len != 0U &&
        httpd_ws_recv_frame(request, &frame, frame.len) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

void EspIdfWebTransport::realtimeWork(void* argument) {
    RealtimeWork* work = static_cast<RealtimeWork*>(argument);
    if (work == nullptr || work->owner == nullptr) return;
    EspIdfWebTransport& owner = *work->owner;
    httpd_handle_t server = nullptr;
    if (owner.realtimeMutex_ != nullptr &&
        xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        if (owner.accepting_.load() && owner.server_ != nullptr) {
            server = owner.server_;
        }
        xSemaphoreGive(owner.realtimeMutex_);
    }

    RealtimeClientToken tokens[REALTIME_CLIENT_CAPACITY] {};
    int clients[REALTIME_CLIENT_CAPACITY] {};
    size_t count = 0U;
    if (server != nullptr && owner.realtimeMutex_ != nullptr &&
        xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        if (owner.realtimeRecovery_.acceptsGeneration(
                work->recoveryGeneration)) {
            for (size_t i = 0U; i < REALTIME_CLIENT_CAPACITY; ++i) {
                const RealtimeClientRegistry<REALTIME_CLIENT_CAPACITY>::Slot* slot =
                    owner.realtimeClients_.slot(i);
                if (slot != nullptr && slot->state == RealtimeClientState::Live) {
                    tokens[count] = RealtimeClientToken {i, slot->generation};
                    clients[count] = slot->fd;
                    ++count;
                }
            }
        }
        xSemaphoreGive(owner.realtimeMutex_);
    }
    httpd_ws_frame_t frame {};
    frame.type = work->type == RealtimeFrameType::Text ?
        HTTPD_WS_TYPE_TEXT : HTTPD_WS_TYPE_BINARY;
    frame.payload = work->payload;
    frame.len = work->length;
    for (size_t i = 0U; i < count; ++i) {
        if (!owner.realtimeRecovery_.acceptsGeneration(
                work->recoveryGeneration)) {
            break;
        }
        const bool active =
            httpd_ws_get_fd_info(server, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET;
        if (!active ||
            httpd_ws_send_frame_async(server, clients[i], &frame) != ESP_OK) {
            if (xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
                if (active) {
                    owner.realtimeClients_.sendFailed(tokens[i]);
                } else {
                    owner.realtimeClients_.reclaimInactive(tokens[i], clients[i]);
                }
                xSemaphoreGive(owner.realtimeMutex_);
            }
            if (active) {
                const esp_err_t closeResult =
                    httpd_sess_trigger_close(server, clients[i]);
                if (xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
                    if (closeResult == ESP_OK) {
                        owner.realtimeClients_.markCloseRequested(
                            tokens[i], clients[i]);
                    } else if (closeResult == ESP_ERR_NOT_FOUND) {
                        owner.realtimeClients_.reclaimInactive(
                            tokens[i], clients[i]);
                    }
                    xSemaphoreGive(owner.realtimeMutex_);
                }
            }
        }
    }
    if (owner.realtimeMutex_ != nullptr &&
        xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        work->inUse.store(false);
        xSemaphoreGive(owner.realtimeMutex_);
    }
}

void EspIdfWebTransport::streamStartWork(void* argument) {
    StreamStartWork* work = static_cast<StreamStartWork*>(argument);
    if (work == nullptr || work->owner == nullptr) return;
    EspIdfWebTransport& owner = *work->owner;
    httpd_handle_t server = nullptr;
    int fd = -1;
    bool maySend = false;
    if (owner.realtimeMutex_ != nullptr &&
        xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        const RealtimeClientRegistry<REALTIME_CLIENT_CAPACITY>::Slot* slot =
            owner.realtimeClients_.slot(work->token.slot);
        if (owner.accepting_.load() && owner.server_ != nullptr &&
            owner.realtimeRecovery_.acceptsGeneration(
                work->recoveryGeneration) &&
            owner.realtimeClients_.isTokenCurrent(work->token) &&
            slot->state == RealtimeClientState::Connecting &&
            !slot->missedWhileConnecting) {
            server = owner.server_;
            fd = slot->fd;
            maySend = true;
        } else {
            owner.realtimeClients_.markerFailed(work->token);
        }
        xSemaphoreGive(owner.realtimeMutex_);
    }
    bool sent = false;
    const bool active = maySend &&
        httpd_ws_get_fd_info(server, fd) == HTTPD_WS_CLIENT_WEBSOCKET;
    if (active) {
        httpd_ws_frame_t frame {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = work->payload;
        frame.len = work->length;
        sent = httpd_ws_send_frame_async(server, fd, &frame) == ESP_OK;
    }
    bool live = false;
    if (owner.realtimeMutex_ != nullptr &&
        xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
        live = sent && owner.realtimeRecovery_.acceptsGeneration(
            work->recoveryGeneration) &&
            owner.realtimeClients_.markerSucceeded(work->token, false);
        if (!active) {
            owner.realtimeClients_.reclaimInactive(work->token, fd);
        } else if (!live) {
            owner.realtimeClients_.markerFailed(work->token);
        }
        work->inUse.store(false);
        xSemaphoreGive(owner.realtimeMutex_);
    }
    if (!live && active && server != nullptr && fd >= 0) {
        const esp_err_t closeResult = httpd_sess_trigger_close(server, fd);
        if (owner.realtimeMutex_ != nullptr &&
            xSemaphoreTake(owner.realtimeMutex_, portMAX_DELAY) == pdTRUE) {
            if (closeResult == ESP_OK) {
                owner.realtimeClients_.markCloseRequested(work->token, fd);
            } else if (closeResult == ESP_ERR_NOT_FOUND) {
                owner.realtimeClients_.reclaimInactive(work->token, fd);
            }
            xSemaphoreGive(owner.realtimeMutex_);
        }
    }
}

esp_err_t EspIdfWebTransport::dispatch(httpd_req_t* request) {
    if (request == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    EspIdfWebTransport* owner = static_cast<EspIdfWebTransport*>(
        httpd_get_global_user_ctx(request->handle)
    );
    const HttpRouteRegistry::Route* route =
        static_cast<const HttpRouteRegistry::Route*>(request->user_ctx);
    if (owner == nullptr || !owner->accepting_.load()) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_send(
            request, "Service Unavailable", HTTPD_RESP_USE_STRLEN
        );
    }
    if (route == nullptr || route->handler == nullptr) {
        return httpd_resp_send_500(request);
    }
    if (request->content_len > route->options.maxBodyLength) {
        return rejectOversizeBody(request);
    }

    size_t total = 0U;
    while (total < request->content_len) {
        const size_t remaining = request->content_len - total;
        const ssize_t received = httpd_req_recv(
            request, owner->normalBody_ + total, remaining
        );
        // Timeouts and socket errors are terminal for this bounded request.
        // Retrying here could make the HTTPD task wait without a local bound.
        if (received <= 0) {
            return rejectUnreadBody(request, "400 Bad Request",
                                    "Incomplete request body");
        }
        total += static_cast<size_t>(received);
    }
    owner->normalBody_[total] = '\0';

    IdfResponseWriter response(*request);
    IdfRequestContext context(*request, response);
    const HttpRouteRequest view {
        route->method,
        request->uri,
        total == 0U ? nullptr : owner->normalBody_,
        total,
        &context
    };
    route->handler(route->context, view, response);
    return response.finish();
}

esp_err_t EspIdfWebTransport::streamDispatch(httpd_req_t* request) {
    static_assert(sizeof(streamBody_) == Internal::HTTP_STREAM_CHUNK_CAPACITY,
                  "stream receive storage must match the fixed read size");
    if (request == nullptr) return ESP_ERR_INVALID_ARG;
    EspIdfWebTransport* owner = static_cast<EspIdfWebTransport*>(
        httpd_get_global_user_ctx(request->handle)
    );
    const HttpRouteRegistry::Route* route =
        static_cast<const HttpRouteRegistry::Route*>(request->user_ctx);
    if (owner == nullptr || !owner->accepting_.load() || route == nullptr ||
        route->kind != HttpRouteRegistry::RouteKind::Streaming ||
        route->streamHandler == nullptr) return ESP_FAIL;
    IdfResponseWriter response(*request);
    IdfRequestContext context(*request, response);
    const HttpStreamRequest view {
        route->method, request->uri, request->content_len, &context
    };
    const Internal::StreamReceiveResult result = Internal::receiveStream(
        view, route->options.maxBodyLength, route->streamHandler,
        route->context, response, owner->streamBody_, streamRead, request,
        streamAccepting, owner, HTTPD_SOCK_ERR_TIMEOUT,
        streamOversize, request
    );
    if (result == Internal::StreamReceiveResult::Complete ||
        result == Internal::StreamReceiveResult::StoppedConsumed)
        return response.finish();
    return ESP_FAIL;
}

esp_err_t EspIdfWebTransport::notFound(
    httpd_req_t* request, httpd_err_code_t error
) {
    if (request == nullptr || error != HTTPD_404_NOT_FOUND) {
        return ESP_FAIL;
    }
    EspIdfWebTransport* owner = static_cast<EspIdfWebTransport*>(
        httpd_get_global_user_ctx(request->handle)
    );
    // A 404 request can also carry an unread body. Close its connection
    // after responding so those bytes cannot become the next request.
    if (request->content_len != 0U) {
        httpd_resp_send_404(request);
        return ESP_FAIL;
    }
    if (owner == nullptr || !owner->accepting_.load() ||
        owner->routes_.notFoundHandler() == nullptr) {
        return httpd_resp_send_404(request);
    }
    IdfResponseWriter response(*request);
    owner->routes_.notFoundHandler()(
        owner->routes_.notFoundContext(), response
    );
    return response.finish();
}

void EspIdfWebTransport::keepBorrowedContext(void*) {}

} // namespace Web
} // namespace AquaCore
