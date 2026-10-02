#pragma once

#include <stdint.h>

#include <AquaCore/Commands/CommandClass.h>
#include <AquaCore/Maintenance/MaintenanceTransition.h>
#include <AquaCore/System/Startup.h>

namespace AquaCore {
namespace Commands {

enum class MaintenanceCommandKind : uint8_t {
    Enter,
    Operation,
    Exit
};

// Maintenance operational-state policy only. The Composition Root owns this
// gate and its borrowed readers/resolvers for the policy path lifetime.
// Safety remains a separate, later pipeline stage.
template <typename Command>
class RuntimeMaintenanceCommandPolicyGate {
public:
    using StatusReader = bool (*)(const void*, System::RuntimeStatus&);
    using ClassResolver = bool (*)(const Command&, const void*, CommandClass&);
    using KindResolver = bool (*)(
        const Command&, const void*, MaintenanceCommandKind&);

    RuntimeMaintenanceCommandPolicyGate(
        StatusReader readStatus,
        const void* statusContext,
        ClassResolver resolveClass,
        const void* classContext,
        KindResolver resolveKind,
        const void* kindContext
    ) : readStatus_(readStatus),
        statusContext_(statusContext),
        resolveClass_(resolveClass),
        classContext_(classContext),
        resolveKind_(resolveKind),
        kindContext_(kindContext) {
    }

    RuntimeMaintenanceCommandPolicyGate(
        const RuntimeMaintenanceCommandPolicyGate&) = delete;
    RuntimeMaintenanceCommandPolicyGate& operator=(
        const RuntimeMaintenanceCommandPolicyGate&) = delete;
    RuntimeMaintenanceCommandPolicyGate(
        RuntimeMaintenanceCommandPolicyGate&&) = delete;
    RuntimeMaintenanceCommandPolicyGate& operator=(
        RuntimeMaintenanceCommandPolicyGate&&) = delete;

    bool allows(const Command& command) const {
        if (readStatus_ == nullptr || resolveClass_ == nullptr ||
            resolveKind_ == nullptr) {
            return false;
        }

        System::RuntimeStatus status {};
        if (!readStatus_(statusContext_, status)) {
            return false;
        }

        CommandClass commandClass = CommandClass::NormalDomain;
        if (!resolveClass_(command, classContext_, commandClass) ||
            commandClass != CommandClass::Maintenance) {
            return false;
        }

        MaintenanceCommandKind kind = MaintenanceCommandKind::Enter;
        if (!resolveKind_(command, kindContext_, kind)) {
            return false;
        }

        switch (kind) {
            case MaintenanceCommandKind::Enter:
                return allowsTransition(
                    status.operational,
                    Maintenance::MaintenanceTransitionRequest::Enter
                );
            case MaintenanceCommandKind::Operation:
                return status.operational == System::OperationalState::MAINTENANCE;
            case MaintenanceCommandKind::Exit:
                return allowsTransition(
                    status.operational,
                    Maintenance::MaintenanceTransitionRequest::Exit
                );
            default:
                return false;
        }
    }

    static bool callback(const Command& command, void* context) {
        return context != nullptr &&
               static_cast<const RuntimeMaintenanceCommandPolicyGate*>(context)
                   ->allows(command);
    }

private:
    static bool allowsTransition(
        System::OperationalState current,
        Maintenance::MaintenanceTransitionRequest request
    ) {
        const Maintenance::MaintenanceTransitionDecision decision =
            Maintenance::evaluateMaintenanceTransition(current, request);
        return decision == Maintenance::MaintenanceTransitionDecision::Allowed ||
               decision == Maintenance::MaintenanceTransitionDecision::AlreadyInTargetState;
    }

    StatusReader readStatus_;
    const void* statusContext_;
    ClassResolver resolveClass_;
    const void* classContext_;
    KindResolver resolveKind_;
    const void* kindContext_;
};

} // namespace Commands
} // namespace AquaCore
