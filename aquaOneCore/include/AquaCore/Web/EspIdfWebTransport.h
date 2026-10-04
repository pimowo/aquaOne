#pragma once

#include <atomic>

#include <esp_http_server.h>

#include "AquaCore/Web/HttpRouteRegistry.h"

namespace AquaCore {
namespace Web {

// Application-owned physical HTTPD transport. Route callbacks run in the
// HTTPD server task, never implicitly in the Application loop. F9.2 routes
// must not read mutable Domain state or invoke Domain/hardware actions.
// Lifecycle methods and destruction belong to the Application/composition
// context, never to a route callback or the HTTPD server task: httpd_stop()
// waits for that task to exit. Calls from the owner are serialized.
class EspIdfWebTransport final {
public:
    EspIdfWebTransport() = default;
    ~EspIdfWebTransport();
    EspIdfWebTransport(const EspIdfWebTransport&) = delete;
    EspIdfWebTransport& operator=(const EspIdfWebTransport&) = delete;

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context = nullptr);
    bool setNotFoundHandler(HttpNotFoundHandler handler,
                            void* context = nullptr);

    // Same-port repeat is idempotent. Different-port repeat fails. Failed
    // starts leave no running server; stop allows retry with frozen routes.
    bool begin(uint16_t port);
    void stop();
    bool isRunning() const { return accepting_.load() && server_ != nullptr; }

private:
    static esp_err_t dispatch(httpd_req_t* request);
    static esp_err_t notFound(httpd_req_t* request, httpd_err_code_t error);
    static void keepBorrowedContext(void*);

    HttpRouteRegistry routes_;
    httpd_handle_t server_ = nullptr;
    uint16_t port_ = 0U;
    std::atomic<bool> accepting_ {false};
};

} // namespace Web
} // namespace AquaCore
