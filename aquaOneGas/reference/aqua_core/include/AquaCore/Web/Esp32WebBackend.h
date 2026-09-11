#pragma once

#include "AquaCore/Web/WebBackend.h"

namespace AquaCore {
namespace Web {

class Esp32WebBackend final : public WebBackend {
public:
    Esp32WebBackend();
    ~Esp32WebBackend() override;
    Esp32WebBackend(const Esp32WebBackend&) = delete;
    Esp32WebBackend& operator=(const Esp32WebBackend&) = delete;

    bool addRoute(
        const char* path,
        HttpMethod method,
        WebRouteHandler handler,
        void* context
    ) override;
    bool setNotFoundHandler(
        WebRouteHandler handler,
        void* context
    ) override;
    bool begin(uint16_t port) override;
    void update() override;
    void stop() override;
    bool isRunning() const override;

private:
    class Impl;
    Impl* impl_;
};

} // namespace Web
} // namespace AquaCore
