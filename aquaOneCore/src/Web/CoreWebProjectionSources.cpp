#include "AquaCore/Web/CoreWebProjectionSources.h"

#include <cstring>

namespace AquaCore {
namespace Web {

bool SystemServiceWebProjectionSource::read(CoreSystemProjection& out) const {
    if (!source_.isReady()) {
        return false;
    }
    CoreSystemProjection value {};
    value.identity = source_.deviceIdentity();
    value.canonicalIdentity = canonicalIdentity_;
    value.apiProtocolVersion = apiProtocolVersion_;
    std::strncpy(
        value.aquaCoreVersion,
        source_.aquaCoreVersion(),
        sizeof(value.aquaCoreVersion) - 1U
    );
    value.uptimeMs = source_.uptimeMs();
    value.restartReason = source_.restartReason();
    value.ready = source_.isReady();
    out = value;
    return true;
}

bool DiagnosticsServiceWebProjectionSource::read(
    CoreDiagnosticsProjection& out
) const {
    CoreDiagnosticsProjection value {};
    value.value = source_.snapshot();
    out = value;
    return true;
}

} // namespace Web
} // namespace AquaCore
