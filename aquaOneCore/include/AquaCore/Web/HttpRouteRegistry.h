#pragma once

#include <stddef.h>
#include <string.h>

#include "AquaCore/Web/WebTypes.h"

namespace AquaCore {
namespace Web {

// The request view and writer are valid only during the transport callback.
// F9.2 accepts bodyless GET/POST only; no HTTPD or Domain type crosses here.
struct HttpRouteRequest {
    HttpMethod method;
    const char* path;
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

    struct Route {
        char path[MAX_PATH_LENGTH + 1U] {};
        HttpMethod method = HttpMethod::Get;
        HttpRouteHandler handler = nullptr;
        void* context = nullptr;
    };

    HttpRouteRegistry() = default;
    HttpRouteRegistry(const HttpRouteRegistry&) = delete;
    HttpRouteRegistry& operator=(const HttpRouteRegistry&) = delete;

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context) {
        if (frozen_ || !validPath(path) || !validMethod(method) ||
            handler == nullptr || count_ == MAX_ROUTES) {
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
        route.handler = handler;
        route.context = context;
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
