#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Web {
namespace Internal {

// The HTTPD handle remains valid when its shutdown signal cannot be sent.
// Keep it (and the port) until a later attempt actually completes teardown.
template <typename Handle>
bool stopRetainingHandle(Handle& handle, uint16_t& port,
                         bool (*stopOperation)(Handle, void*), void* context) {
    if (handle != nullptr &&
        (stopOperation == nullptr || !stopOperation(handle, context)))
        return false;
    handle = nullptr;
    port = 0U;
    return true;
}

template <typename Handle>
bool canStartNewServer(Handle handle) {
    return handle == nullptr;
}

enum class DestructionDisposition : uint8_t {
    Safe,
    MustFailSafe
};

template <typename Handle>
DestructionDisposition destructionDisposition(Handle handle) {
    return handle == nullptr ? DestructionDisposition::Safe
                             : DestructionDisposition::MustFailSafe;
}

} // namespace Internal
} // namespace Web
} // namespace AquaCore
