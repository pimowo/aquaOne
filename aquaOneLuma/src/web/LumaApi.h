#pragma once

#include <stdint.h>

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/Web/WebApiProvider.h"

#include "../app/FirmwareApp.h"

namespace LumaSense {
namespace Web {

class StatusApi final : public AquaCore::Web::WebApiProvider {
public:
    StatusApi(
        const FirmwareApp& app,
        const AquaCore::Network::NetworkService& network,
        const AquaCore::Diagnostics::DiagnosticsService& diagnostics
    );

    const char* route() const override;
    AquaCore::Web::HttpMethod method() const override;
    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

private:
    const FirmwareApp& app_;
    const AquaCore::Network::NetworkService& network_;
    const AquaCore::Diagnostics::DiagnosticsService& diagnostics_;
};

class ModeApi final : public AquaCore::Web::WebApiProvider {
public:
    explicit ModeApi(FirmwareApp& app);

    const char* route() const override;
    AquaCore::Web::HttpMethod method() const override;
    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

private:
    FirmwareApp& app_;
};

class ProfileApi final : public AquaCore::Web::WebApiProvider {
public:
    explicit ProfileApi(FirmwareApp& app);

    const char* route() const override;
    AquaCore::Web::HttpMethod method() const override;
    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

private:
    FirmwareApp& app_;
};

class ManualApi final : public AquaCore::Web::WebApiProvider {
public:
    ManualApi(FirmwareApp& app, const uint32_t& nowMs);

    const char* route() const override;
    AquaCore::Web::HttpMethod method() const override;
    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

private:
    FirmwareApp& app_;
    const uint32_t& nowMs_;
};

} // namespace Web
} // namespace LumaSense
