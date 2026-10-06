#pragma once

#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Web/CoreWebProjections.h"

namespace AquaCore {
namespace Web {

// Transitional Application-side adapters. They may read mutable authorities
// only from the serialized Application context and copy complete bounded values.
class SystemServiceWebProjectionSource final
    : public CoreSystemProjectionSource {
public:
    explicit SystemServiceWebProjectionSource(const SystemService& source)
        : source_(source) {}
    bool read(CoreSystemProjection& out) const override;

private:
    const SystemService& source_;
};

class DiagnosticsServiceWebProjectionSource final
    : public CoreDiagnosticsProjectionSource {
public:
    explicit DiagnosticsServiceWebProjectionSource(
        const Diagnostics::DiagnosticsService& source
    ) : source_(source) {}
    bool read(CoreDiagnosticsProjection& out) const override;

private:
    const Diagnostics::DiagnosticsService& source_;
};

} // namespace Web
} // namespace AquaCore
