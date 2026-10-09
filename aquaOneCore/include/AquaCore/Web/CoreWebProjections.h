#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "AquaCore/Diagnostics/DiagnosticsTypes.h"
#include "AquaCore/System/DeviceIdentity.h"
#include "AquaCore/System/Identity.h"
#include "AquaCore/System/RestartReason.h"
#include "AquaCore/Web/ApiProtocolVersion.h"

namespace AquaCore {
namespace Web {

struct CoreSystemProjection {
    static constexpr size_t CORE_VERSION_CAPACITY =
        Diagnostics::VERSION_TEXT_CAPACITY;

    DeviceIdentity identity {};
    Identity::DeviceIdentity canonicalIdentity {};
    ApiProtocolVersion apiProtocolVersion {DEFAULT_API_PROTOCOL_VERSION};
    char aquaCoreVersion[CORE_VERSION_CAPACITY] {};
    uint32_t uptimeMs = 0U;
    RestartReason restartReason = RestartReason::Unknown;
    bool ready = false;
};

struct CoreDiagnosticsProjection {
    Diagnostics::DiagnosticsSnapshot value {};
};

static_assert(
    std::is_trivially_copyable<CoreSystemProjection>::value,
    "CoreSystemProjection must remain a bounded copy value"
);
static_assert(
    std::is_trivially_copyable<CoreDiagnosticsProjection>::value,
    "CoreDiagnosticsProjection must remain a bounded copy value"
);

class CoreSystemProjectionSource {
public:
    virtual bool read(CoreSystemProjection& out) const = 0;

protected:
    ~CoreSystemProjectionSource() = default;
};

class CoreDiagnosticsProjectionSource {
public:
    virtual bool read(CoreDiagnosticsProjection& out) const = 0;

protected:
    ~CoreDiagnosticsProjectionSource() = default;
};

} // namespace Web
} // namespace AquaCore
