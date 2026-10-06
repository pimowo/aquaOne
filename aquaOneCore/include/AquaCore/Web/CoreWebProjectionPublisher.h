#pragma once

#include "AquaCore/Web/CoreWebProjections.h"
#include "AquaCore/Web/PublishedSnapshot.h"

namespace AquaCore {
namespace Web {

struct CoreWebPublicationResult {
    bool systemPublished = false;
    bool diagnosticsPublished = false;
};

// Application calls update() from its serialized tick. Sources and snapshot
// targets are borrowed and must outlive this publisher.
class CoreWebProjectionPublisher {
public:
    CoreWebProjectionPublisher(
        const CoreSystemProjectionSource& systemSource,
        const CoreDiagnosticsProjectionSource* diagnosticsSource,
        PublishedSnapshot<CoreSystemProjection>& systemTarget,
        PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsTarget
    );

    CoreWebPublicationResult update();

private:
    const CoreSystemProjectionSource& systemSource_;
    const CoreDiagnosticsProjectionSource* diagnosticsSource_;
    PublishedSnapshot<CoreSystemProjection>& systemTarget_;
    PublishedSnapshot<CoreDiagnosticsProjection>& diagnosticsTarget_;
};

} // namespace Web
} // namespace AquaCore
