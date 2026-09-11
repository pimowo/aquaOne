#include "AquaCore/Web/Esp32WebBackend.h"

#include <cstring>
#include <new>

#include <WebServer.h>

namespace AquaCore {
namespace Web {
namespace {

constexpr size_t MAX_ROUTES = 24U;
constexpr size_t MAX_PATH_LENGTH = 63U;

HTTPMethod toEspMethod(HttpMethod method) {
    return method == HttpMethod::Post
        ? HTTP_POST
        : HTTP_GET;
}

HttpMethod fromEspMethod(HTTPMethod method) {
    return method == HTTP_POST
        ? HttpMethod::Post
        : HttpMethod::Get;
}

class Esp32ResponseWriter final : public WebResponseWriter {
public:
    explicit Esp32ResponseWriter(WebServer& server)
        : server_(server) {
    }

    bool beginResponse(
        uint16_t statusCode,
        ContentType contentType
    ) override {
        if (started_) {
            return false;
        }

        server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server_.send(
            static_cast<int>(statusCode),
            contentTypeName(contentType),
            ""
        );
        started_ = true;
        return true;
    }

    bool write(const char* data, size_t length) override {
        if (!started_ || ended_ || (data == nullptr && length > 0U)) {
            return false;
        }

        if (length > 0U) {
            server_.sendContent(data, length);
        }
        return true;
    }

    bool endResponse() override {
        if (!started_ || ended_) {
            return false;
        }

        server_.sendContent("", 0U);
        ended_ = true;
        return true;
    }

private:
    WebServer& server_;
    bool started_ = false;
    bool ended_ = false;
};

} // namespace

class Esp32WebBackend::Impl {
public:
    ~Impl() {
        stop();
    }

    bool addRoute(
        const char* path,
        HttpMethod method,
        WebRouteHandler handler,
        void* context
    ) {
        if (
            path == nullptr ||
            handler == nullptr ||
            path[0] != '/' ||
            std::strlen(path) > MAX_PATH_LENGTH ||
            routeCount_ >= MAX_ROUTES
        ) {
            return false;
        }

        for (size_t i = 0U; i < routeCount_; ++i) {
            if (
                routes_[i].method == method &&
                std::strcmp(routes_[i].path, path) == 0
            ) {
                return false;
            }
        }

        Route& route = routes_[routeCount_];
        std::strncpy(route.path, path, sizeof(route.path) - 1U);
        route.path[sizeof(route.path) - 1U] = '\0';
        route.method = method;
        route.handler = handler;
        route.context = context;

        const size_t index = routeCount_;
        ++routeCount_;

        if (running_) {
            installRoute(index);
        }
        return true;
    }

    bool setNotFoundHandler(
        WebRouteHandler handler,
        void* context
    ) {
        if (handler == nullptr) {
            return false;
        }

        notFoundHandler_ = handler;
        notFoundContext_ = context;
        if (running_) {
            installNotFound();
        }
        return true;
    }

    bool begin(uint16_t port) {
        if (port == 0U) {
            return false;
        }

        if (running_) {
            return port_ == port;
        }

        server_ = new (std::nothrow) WebServer(port);
        if (server_ == nullptr) {
            return false;
        }

        port_ = port;
        for (size_t i = 0U; i < routeCount_; ++i) {
            installRoute(i);
        }
        installNotFound();
        server_->begin();
        running_ = true;
        return true;
    }

    void update() {
        if (running_ && server_ != nullptr) {
            server_->handleClient();
        }
    }

    void stop() {
        if (server_ != nullptr) {
            if (running_) {
                server_->stop();
            }
            delete server_;
            server_ = nullptr;
        }
        running_ = false;
        port_ = 0U;
    }

    bool isRunning() const {
        return running_;
    }

private:
    struct Route {
        char path[MAX_PATH_LENGTH + 1U] {};
        HttpMethod method = HttpMethod::Get;
        WebRouteHandler handler = nullptr;
        void* context = nullptr;
    };

    void installRoute(size_t index) {
        if (server_ == nullptr || index >= routeCount_) {
            return;
        }

        server_->on(
            routes_[index].path,
            toEspMethod(routes_[index].method),
            [this, index]() {
                dispatch(routes_[index]);
            }
        );
    }

    void installNotFound() {
        if (server_ == nullptr || notFoundHandler_ == nullptr) {
            return;
        }

        server_->onNotFound([this]() {
            String path = server_->uri();
            String body;
            if (
                server_->method() == HTTP_POST &&
                server_->hasArg("plain")
            ) {
                body = server_->arg("plain");
            }

            WebRequest request {
                fromEspMethod(server_->method()),
                path.c_str(),
                body.length() > 0U ? body.c_str() : nullptr,
                body.length()
            };
            Esp32ResponseWriter response(*server_);
            notFoundHandler_(
                notFoundContext_,
                request,
                response
            );
        });
    }

    void dispatch(const Route& route) {
        String path = server_->uri();
        String body;
        if (
            route.method == HttpMethod::Post &&
            server_->hasArg("plain")
        ) {
            body = server_->arg("plain");
        }

        WebRequest request {
            route.method,
            path.c_str(),
            body.length() > 0U ? body.c_str() : nullptr,
            body.length()
        };
        Esp32ResponseWriter response(*server_);
        route.handler(route.context, request, response);
    }

    Route routes_[MAX_ROUTES] {};
    size_t routeCount_ = 0U;
    WebRouteHandler notFoundHandler_ = nullptr;
    void* notFoundContext_ = nullptr;
    WebServer* server_ = nullptr;
    uint16_t port_ = 0U;
    bool running_ = false;
};

Esp32WebBackend::Esp32WebBackend()
    : impl_(new (std::nothrow) Impl()) {
}

Esp32WebBackend::~Esp32WebBackend() {
    delete impl_;
    impl_ = nullptr;
}

bool Esp32WebBackend::addRoute(
    const char* path,
    HttpMethod method,
    WebRouteHandler handler,
    void* context
) {
    return impl_ != nullptr &&
        impl_->addRoute(path, method, handler, context);
}

bool Esp32WebBackend::setNotFoundHandler(
    WebRouteHandler handler,
    void* context
) {
    return impl_ != nullptr &&
        impl_->setNotFoundHandler(handler, context);
}

bool Esp32WebBackend::begin(uint16_t port) {
    return impl_ != nullptr && impl_->begin(port);
}

void Esp32WebBackend::update() {
    if (impl_ != nullptr) {
        impl_->update();
    }
}

void Esp32WebBackend::stop() {
    if (impl_ != nullptr) {
        impl_->stop();
    }
}

bool Esp32WebBackend::isRunning() const {
    return impl_ != nullptr && impl_->isRunning();
}

} // namespace Web
} // namespace AquaCore
