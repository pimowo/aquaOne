#pragma once

#include <stddef.h>
#include <string.h>

#include "AquaCore/Web/WebTypes.h"
#include "AquaCore/Web/HttpStreamingServerTransport.h"

namespace AquaCore {
namespace Web {

// CURRENT implementation capacity. Product routes still declare their own
// lower wire limits; this does not make 1536 bytes a platform-wide default.
constexpr size_t HTTP_NORMAL_BODY_CAPACITY = 1536U;

struct HttpRouteOptions {
    constexpr explicit HttpRouteOptions(size_t maximum = 0U)
        : maxBodyLength(maximum) {}
    // Zero means that the route accepts no normal request body.
    size_t maxBodyLength;
};

// The request view and writer are valid only during the transport callback;
// no HTTPD or Domain type crosses here. bodyLength is authoritative. The
// transport appends a convenience NUL byte after a non-null body view. Any
// future async/deferred handler must copy the required bytes before returning.
struct HttpRouteRequest {
    HttpMethod method;
    const char* path;
    const char* body;
    size_t bodyLength;
    // Borrowed for this callback only. It must never enter an Application request.
    const WebRequestContext* context;

    bool hasHeader(const char* name) const {
        return context != nullptr && name != nullptr && name[0] != '\0' &&
               context->hasHeader(name);
    }
    // Returns the full header length. On insufficient capacity, output is empty.
    bool copyHeader(const char* name, char* output, size_t capacity,
                    size_t& length) const {
        length = 0U;
        if (output != nullptr && capacity != 0U) output[0] = '\0';
        if (context == nullptr || name == nullptr || name[0] == '\0' ||
            !context->hasHeader(name))
            return false;
        length = context->copyHeader(name, output, capacity);
        return output != nullptr && capacity != 0U && length < capacity &&
               (length == 0U || output[0] != '\0');
    }
    bool authenticateBasic(const char* user, const char* password) const {
        return context != nullptr && user != nullptr && password != nullptr &&
               context->authenticateBasic(user, password);
    }
    bool requestBasicAuthentication(const char* realm) const {
        return context != nullptr && context->requestBasicAuthentication(realm);
    }
};

using HttpRouteHandler = void (*)(
    void* context, const HttpRouteRequest& request, WebResponseWriter& response
);
using HttpNotFoundHandler = void (*)(void* context, WebResponseWriter& response);

// Application owns this fixed table. Contexts are borrowed and must outlive
// every server callback. The table and its entries never move.
class HttpRouteRegistry {
public:
    static constexpr size_t MAX_ROUTES = 24U; // F9.2 implementation capacity.
    static constexpr size_t MAX_PATH_LENGTH = 63U;

    enum class RouteKind : uint8_t { Normal, Streaming };
    struct Route {
        char path[MAX_PATH_LENGTH + 1U] {};
        HttpMethod method = HttpMethod::Get;
        RouteKind kind = RouteKind::Normal;
        HttpRouteHandler handler = nullptr;
        HttpStreamHandler streamHandler = nullptr;
        void* context = nullptr;
        HttpRouteOptions options {};
    };

    HttpRouteRegistry() = default;
    HttpRouteRegistry(const HttpRouteRegistry&) = delete;
    HttpRouteRegistry& operator=(const HttpRouteRegistry&) = delete;

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context) {
        return addRoute(path, method, handler, context, HttpRouteOptions {});
    }

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context,
                  const HttpRouteOptions& options) {
        if (frozen_ || !validPath(path) || !validMethod(method) ||
            handler == nullptr || count_ == MAX_ROUTES ||
            options.maxBodyLength > HTTP_NORMAL_BODY_CAPACITY ||
            (method == HttpMethod::Get && options.maxBodyLength != 0U)) {
            return false;
        }
        for (size_t i = 0U; i < count_; ++i) {
            if (routes_[i].method == method &&
                strcmp(routes_[i].path, path) == 0) {
                return false;
            }
        }
        Route& route = routes_[count_];
        strcpy(route.path, path); // validPath bounded this copy.
        route.method = method;
        route.kind = RouteKind::Normal;
        route.handler = handler;
        route.streamHandler = nullptr;
        route.context = context;
        route.options = options;
        ++count_;
        return true;
    }

    bool addStreamingRoute(const char* path, HttpMethod method,
                           HttpStreamHandler handler, void* context,
                           size_t maxContentLength) {
        if (frozen_ || !validPath(path) || !validMethod(method) ||
            handler == nullptr || count_ == MAX_ROUTES) return false;
        for (size_t i = 0U; i < count_; ++i) {
            if (routes_[i].method == method &&
                strcmp(routes_[i].path, path) == 0) return false;
        }
        Route& route = routes_[count_];
        strcpy(route.path, path);
        route.method = method;
        route.kind = RouteKind::Streaming;
        route.handler = nullptr;
        route.streamHandler = handler;
        route.context = context;
        route.options.maxBodyLength = maxContentLength;
        ++count_;
        return true;
    }

    bool setNotFoundHandler(HttpNotFoundHandler handler, void* context) {
        if (frozen_ || handler == nullptr || notFoundHandler_ != nullptr) {
            return false;
        }
        notFoundHandler_ = handler;
        notFoundContext_ = context;
        return true;
    }

    const Route* routeAt(size_t index) const {
        return index < count_ ? &routes_[index] : nullptr;
    }
    size_t size() const { return count_; }
    HttpNotFoundHandler notFoundHandler() const { return notFoundHandler_; }
    void* notFoundContext() const { return notFoundContext_; }
    bool isFrozen() const { return frozen_; }
    void freeze() { frozen_ = true; }

private:
    static bool validMethod(HttpMethod method) {
        return method == HttpMethod::Get || method == HttpMethod::Post;
    }
    static bool validPath(const char* path) {
        if (path == nullptr || path[0] != '/') {
            return false;
        }
        size_t length = 0U;
        while (path[length] != '\0') {
            if (length == MAX_PATH_LENGTH || path[length] <= ' ') {
                return false;
            }
            ++length;
        }
        return length > 0U;
    }

    Route routes_[MAX_ROUTES] {};
    size_t count_ = 0U;
    HttpNotFoundHandler notFoundHandler_ = nullptr;
    void* notFoundContext_ = nullptr;
    bool frozen_ = false;
};

} // namespace Web
} // namespace AquaCore
