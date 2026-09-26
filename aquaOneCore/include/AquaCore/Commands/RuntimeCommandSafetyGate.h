#pragma once

#include <AquaCore/Safety/ActionLockCoordinator.h>
#include <AquaCore/System/Startup.h>

namespace AquaCore {
namespace Commands {

// Safety callback adapter for normal Domain commands only. Composition Root owns the
// gate, its contexts, and the ActionLockCoordinator for the pipeline lifetime.
// Readers and resolvers are borrowed; nullptr context is valid for stateless
// callbacks. Execute and runtime refresh must use the serialized runtime
// boundary; the gate reads a fresh snapshot on every call.
template <typename Command, typename Action>
class RuntimeCommandSafetyGate {
public:
    using StatusReader = bool (*)(const void*, System::RuntimeStatus&);
    using ActionResolver = bool (*)(const Command&, const void*, Action&);

    RuntimeCommandSafetyGate(
        StatusReader readStatus,
        const void* statusContext,
        ActionResolver resolveAction,
        const void* resolverContext,
        const Safety::ActionLockCoordinator<Action>* actionLocks
    ) : readStatus_(readStatus),
        statusContext_(statusContext),
        resolveAction_(resolveAction),
        resolverContext_(resolverContext),
        actionLocks_(actionLocks) {
    }

    RuntimeCommandSafetyGate(const RuntimeCommandSafetyGate&) = delete;
    RuntimeCommandSafetyGate& operator=(const RuntimeCommandSafetyGate&) = delete;
    RuntimeCommandSafetyGate(RuntimeCommandSafetyGate&&) = delete;
    RuntimeCommandSafetyGate& operator=(RuntimeCommandSafetyGate&&) = delete;

    bool allows(const Command& command) const {
        if (readStatus_ == nullptr || resolveAction_ == nullptr ||
            actionLocks_ == nullptr || !actionLocks_->isCompositionValid()) {
            return false;
        }

        System::RuntimeStatus status {};
        if (!readStatus_(statusContext_, status) ||
            status.safety != System::SafetyState::CLEAR) {
            return false;
        }

        Action action {};
        if (!resolveAction_(command, resolverContext_, action)) {
            return false;
        }

        const Safety::ActionLockState lock = actionLocks_->query(action);
        return lock.compositionValid && !lock.locked;
    }

    // Assign to CommandPipelineConfig<Command>::safety, with a borrowed gate
    // pointer as safetyContext. A missing gate blocks rather than dereferences.
    static bool callback(const Command& command, void* context) {
        if (context == nullptr) {
            return false;
        }
        return static_cast<const RuntimeCommandSafetyGate*>(context)->allows(command);
    }

private:
    StatusReader readStatus_;
    const void* statusContext_;
    ActionResolver resolveAction_;
    const void* resolverContext_;
    const Safety::ActionLockCoordinator<Action>* actionLocks_;
};

} // namespace Commands
} // namespace AquaCore
