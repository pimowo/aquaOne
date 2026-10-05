#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Web {

enum class ActionBridgeWaitStatus : uint8_t {
    Signaled,
    TimedOut,
    Failure
};

// Borrowed synchronization and completion signaling for WebActionBridge.
// All methods are task-context only. The concrete synchronizer must outlive
// the bridge; before destruction no producer may wait and Application must
// have stopped processing commands.
class ActionBridgeSynchronizer {
public:
    virtual bool lock() = 0;
    virtual void unlock() = 0;
    virtual size_t completionSlotCapacity() const = 0;

    // Removes a stale completion signal before a free slot is reused.
    virtual bool prepareCompletion(size_t slotIndex) = 0;
    // Waits no longer than timeoutMs. Slot state remains the completion
    // authority; the bridge rechecks it after every status.
    virtual ActionBridgeWaitStatus waitForCompletion(
        size_t slotIndex, uint32_t timeoutMs
    ) = 0;
    // A false result does not invalidate a result already stored in a slot.
    virtual bool signalCompletion(size_t slotIndex) = 0;

protected:
    ~ActionBridgeSynchronizer() = default;
};

} // namespace Web
} // namespace AquaCore
