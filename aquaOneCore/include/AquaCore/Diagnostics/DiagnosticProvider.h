#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Diagnostics {

enum class DiagnosticReadResult : uint8_t {
    Success,
    Unavailable
};

// Borrowed, read-only projection of current facts into a caller-owned typed
// snapshot. The concrete provider must outlive every consumer holding it.
template <typename Snapshot>
class DiagnosticProvider {
public:
    // Only Success makes out a valid snapshot for this read. For Unavailable
    // or any other result, the caller must ignore out; prior contents need
    // not be preserved. A provider must not retain out after return.
    // Unavailable is not a Health, Safety, Alarm or command decision.
    virtual DiagnosticReadResult read(Snapshot& out) const = 0;

protected:
    // The composition owner destroys the concrete provider, never a consumer
    // through DiagnosticProvider*.
    ~DiagnosticProvider() = default;
};

} // namespace Diagnostics
} // namespace AquaCore
