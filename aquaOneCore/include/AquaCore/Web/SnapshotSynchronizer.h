#pragma once

namespace AquaCore {
namespace Web {

// Borrowed task-to-task exclusion for one or more published Web projections.
// lock() may block and reports failure; callers must never copy without it.
// Not ISR-safe. The concrete synchronizer must outlive every publication using it.
class SnapshotSynchronizer {
public:
    virtual bool lock() = 0;
    virtual void unlock() = 0;

protected:
    ~SnapshotSynchronizer() = default;
};

} // namespace Web
} // namespace AquaCore
