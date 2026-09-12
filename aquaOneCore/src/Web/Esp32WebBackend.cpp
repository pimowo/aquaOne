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

class Esp32RequestContext final : public WebRequestContext {
public:
    explicit Esp32RequestContext(WebServer& server)
        : server_(server) {
    }

    bool hasHeader(const char* name) const override {
        if (name == nullptr || name[0] == '\0') {
            return false;
        }

        for (int index = 0; index < server_.headers(); ++index) {
            if (server_.headerName(index).equalsIgnoreCase(name)) {
                return true;
            }
        }
        return false;
    }

    size_t copyHeader(
        const char* name,
        char* output,
        size_t outputSize
    ) const override {
        if (output != nullptr && outputSize > 0U) {
            output[0] = '\0';
        }
        if (!hasHeader(name)) {
            return 0U;
        }

        const String value = server_.header(name);
        if (output != nullptr && outputSize > 0U) {
            const size_t copyLength = value.length() < outputSize - 1U
                ? value.length()
                : outputSize - 1U;
            std::memcpy(output, value.c_str(), copyLength);
            output[copyLength] = '\0';
        }
        return value.length();
    }

    bool authenticateBasic(
        const char* username,
        const char* password
    ) const override {
        return username != nullptr &&
            password != nullptr &&
            server_.authenticate(username, password);
    }

    bool requestBasicAuthentication(const char* realm) const override {
        server_.requestAuthentication(BASIC_AUTH, realm);
        return true;
    }

private:
    WebServer& server_;
};

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
        return addRoute(
            path,
            method,
            handler,
            context,
            WebRouteOptions {}
        );
    }

    bool addRoute(
        const char* path,
        HttpMethod method,
        WebRouteHandler handler,
        void* context,
        const WebRouteOptions& options
    ) {
        if (
            path == nullptr ||
            handler == nullptr ||
            path[0] != '/' ||
            std::strlen(path) > MAX_PATH_LENGTH ||
            routeCount_ >= MAX_ROUTES ||
            (options.uploadHandler != nullptr && method != HttpMethod::Post) ||
            (options.uploadHandler == nullptr && options.uploadContext != nullptr)
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
        route.options = options;

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
        server_->collectAllHeaders();

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
        WebRouteOptions options {};
        size_t uploadBytes = 0U;
    };

    void installRoute(size_t index) {
        if (server_ == nullptr || index >= routeCount_) {
            return;
        }

        if (routes_[index].options.uploadHandler == nullptr) {
            server_->on(
                routes_[index].path,
                toEspMethod(routes_[index].method),
                [this, index]() {
                    dispatch(routes_[index]);
                }
            );
            return;
        }

        server_->on(
            routes_[index].path,
            toEspMethod(routes_[index].method),
            [this, index]() {
                dispatch(routes_[index]);
            },
            [this, index]() {
                dispatchUpload(routes_[index]);
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
                body.length(),
                nullptr
            };
            Esp32RequestContext requestContext(*server_);
            request.context = &requestContext;
            Esp32ResponseWriter response(*server_);
            notFoundHandler_(
                notFoundContext_,
                request,
                response
            );
        });
    }

    void dispatch(const Route& route) {
        const int contentLength = server_->clientContentLength();
        if (
            route.options.uploadHandler == nullptr &&
            route.options.maxBodyLength > 0U &&
            contentLength > 0 &&
            static_cast<size_t>(contentLength) >
                route.options.maxBodyLength
        ) {
            sendPayloadTooLarge();
            return;
        }

        String path = server_->uri();
        String body;
        if (
            route.method == HttpMethod::Post &&
            server_->hasArg("plain")
        ) {
            body = server_->arg("plain");
        }

        if (
            route.options.uploadHandler == nullptr &&
            route.options.maxBodyLength > 0U &&
            body.length() > route.options.maxBodyLength
        ) {
            sendPayloadTooLarge();
            return;
        }

        WebRequest request {
            route.method,
            path.c_str(),
            body.length() > 0U ? body.c_str() : nullptr,
            body.length(),
            nullptr
        };
        Esp32RequestContext requestContext(*server_);
        request.context = &requestContext;
        Esp32ResponseWriter response(*server_);
        route.handler(route.context, request, response);
    }

    void dispatchUpload(Route& route) {
        if (!server_->header("Content-Type").startsWith("multipart/")) {
            return;
        }

        HTTPUpload& upload = server_->upload();
        if (
            upload.status == UPLOAD_FILE_WRITE &&
            upload.currentSize == 0U
        ) {
            return;
        }

        WebUploadEvent event {};

        switch (upload.status) {
            case UPLOAD_FILE_START:
                route.uploadBytes = 0U;
                event.status = WebUploadStatus::Start;
                break;
            case UPLOAD_FILE_WRITE:
                route.uploadBytes += upload.currentSize;
                event.status = WebUploadStatus::Chunk;
                event.data = upload.buf;
                event.dataLength = upload.currentSize;
                break;
            case UPLOAD_FILE_END:
                event.status = WebUploadStatus::End;
                break;
            case UPLOAD_FILE_ABORTED:
                event.status = WebUploadStatus::Abort;
                break;
        }

        WebRequest request {
            route.method,
            route.path,
            nullptr,
            0U,
            nullptr
        };
        Esp32RequestContext requestContext(*server_);
        request.context = &requestContext;
        event.filename = upload.filename.c_str();
        event.bytesReceived = route.uploadBytes;
        route.options.uploadHandler(
            route.options.uploadContext,
            request,
            event
        );

        if (
            upload.status == UPLOAD_FILE_END ||
            upload.status == UPLOAD_FILE_ABORTED
        ) {
            route.uploadBytes = 0U;
        }
    }

    void sendPayloadTooLarge() {
        server_->send(
            413,
            contentTypeName(ContentType::PlainText),
            "Payload Too Large"
        );
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

bool Esp32WebBackend::addRoute(
    const char* path,
    HttpMethod method,
    WebRouteHandler handler,
    void* context,
    const WebRouteOptions& options
) {
    return impl_ != nullptr &&
        impl_->addRoute(path, method, handler, context, options);
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
