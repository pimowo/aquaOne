#pragma once

#include "AquaCore/Web/WebPageProvider.h"

namespace LumaSense {
namespace Web {

class DashboardPage final : public AquaCore::Web::WebPageProvider {
public:
    const char* route() const override;
    const char* title() const override;
    void render(AquaCore::Web::WebResponseWriter& response) const override;
};

class ControlPage final : public AquaCore::Web::WebPageProvider {
public:
    const char* route() const override;
    const char* title() const override;
    void render(AquaCore::Web::WebResponseWriter& response) const override;
};

class DiagnosticsPage final : public AquaCore::Web::WebPageProvider {
public:
    const char* route() const override;
    const char* title() const override;
    void render(AquaCore::Web::WebResponseWriter& response) const override;
};

class SystemPage final : public AquaCore::Web::WebPageProvider {
public:
    const char* route() const override;
    const char* title() const override;
    void render(AquaCore::Web::WebResponseWriter& response) const override;
};

} // namespace Web
} // namespace LumaSense
