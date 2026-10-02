#pragma once

#include <stddef.h>

namespace AquaCore {
namespace Diagnostics {

// Application-side enumeration of a caller-owned, immutable entry array.
// The array must outlive this view and every consumer of an entry pointer.
// Entry meaning and any borrowed provider references belong to the project.
template <typename Entry>
class DiagnosticRegistry {
public:
    DiagnosticRegistry(const Entry* entries, size_t count)
        : entries_(entries), count_(count) {
    }

    // An empty null view is valid; a nonempty null view is invalid.
    bool isValid() const {
        return entries_ != nullptr || count_ == 0U;
    }

    // Invalid composition exposes no entries.
    size_t size() const {
        return isValid() ? count_ : 0U;
    }

    const Entry* entryAt(size_t index) const {
        return index < size() ? &entries_[index] : nullptr;
    }

private:
    const Entry* const entries_;
    const size_t count_;
};

} // namespace Diagnostics
} // namespace AquaCore
