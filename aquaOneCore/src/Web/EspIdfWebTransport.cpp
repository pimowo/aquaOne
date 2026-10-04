#include "AquaCore/Web/EspIdfWebTransport.h"

#include <limits.h>

namespace AquaCore {
namespace Web {
namespace {

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

esp_err_t bodyUnsupported(httpd_req_t* request) {
    httpd_resp_set_status(request, "413 Payload Too Large");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    const esp_err_t result = httpd_resp_send(
        request, "Request body not supported", HTTPD_RESP_USE_STRLEN
    );
    // No body was consumed. Close this connection so it cannot pollute the
    // next request; F9.4 will introduce bounded owned command input.
    return result == ESP_OK ? ESP_FAIL : result;
}

} // namespace

EspIdfWebTransport::~EspIdfWebTransport() {
    stop();
}

bool EspIdfWebTransport::addRoute(
    const char* path, HttpMethod method,
    HttpRouteHandler handler, void* context
) {
    return server_ == nullptr &&
        routes_.addRoute(path, method, handler, context);
}

bool EspIdfWebTransport::setNotFoundHandler(
    HttpNotFoundHandler handler, void* context
) {
    return server_ == nullptr &&
        routes_.setNotFoundHandler(handler, context);
}

bool EspIdfWebTransport::begin(uint16_t port) {
    if (port == 0U) {
        return false;
    }
    if (server_ != nullptr) {
        return accepting_.load() && port_ == port;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
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

    for (size_t i = 0U; i < routes_.size(); ++i) {
        const HttpRouteRegistry::Route* route = routes_.routeAt(i);
        httpd_uri_t uri {};
        uri.uri = route->path;
        uri.method = route->method == HttpMethod::Get ? HTTP_GET : HTTP_POST;
        uri.handler = dispatch;
        uri.user_ctx = const_cast<HttpRouteRegistry::Route*>(route);
        if (httpd_register_uri_handler(server_, &uri) != ESP_OK) {
            stop();
            return false;
        }
    }

    if (httpd_register_err_handler(
            server_, HTTPD_404_NOT_FOUND, notFound
        ) != ESP_OK) {
        stop();
        return false;
    }

    port_ = port;
    routes_.freeze();
    accepting_.store(true);
    return true;
}

void EspIdfWebTransport::stop() {
    accepting_.store(false);
    if (server_ != nullptr) {
        httpd_stop(server_); // Waits for the HTTPD task to finish callbacks.
        server_ = nullptr;
    }
    port_ = 0U;
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
    if (request->content_len != 0U) {
        return bodyUnsupported(request);
    }

    IdfResponseWriter response(*request);
    const HttpRouteRequest view {
        route->method,
        request->uri
    };
    route->handler(route->context, view, response);
    return response.finish();
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
