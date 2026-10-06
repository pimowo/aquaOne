#include "AquaCore/Web/CoreWebProjectionPublisher.h"

namespace AquaCore {
namespace Web {

CoreWebProjectionPublisher::CoreWebProjectionPublisher(
    const CoreSystemProjectionSource& systemSource,
    const CoreDiagnosticsProjectionSource* diagnosticsSource,
    PublishedSnapshot<CoreSystemProjection>& systemTarget,
    PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsTarget
) : systemSource_(systemSource),
    diagnosticsSource_(diagnosticsSource),
    systemTarget_(systemTarget),
    diagnosticsTarget_(diagnosticsTarget) {}

CoreWebPublicationResult CoreWebProjectionPublisher::update() {
    CoreWebPublicationResult result {};
    CoreSystemProjection system {};
    if (systemSource_.read(system)) {
        result.systemPublished = systemTarget_.publish(system);
    } else {
        systemTarget_.invalidate();
    }

    CoreDiagnosticsProjection diagnostics {};
    if (diagnosticsSource_ != nullptr && diagnosticsSource_->read(diagnostics)) {
        result.diagnosticsPublished = diagnosticsTarget_.publish(diagnostics);
    } else {
        diagnosticsTarget_.invalidate();
    }
    return result;
}

} // namespace Web
} // namespace AquaCore
