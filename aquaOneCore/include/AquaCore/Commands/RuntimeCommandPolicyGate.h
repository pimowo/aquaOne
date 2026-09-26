#pragma once

#include <AquaCore/Commands/CommandClass.h>
#include <AquaCore/System/Startup.h>

namespace AquaCore {
namespace Commands {

// Operational-state policy only. Composition Root owns this gate and its
// borrowed reader/resolver contexts for the pipeline lifetime. routeClass
// confines one gate instance to one handler path. SystemRecovery
// safety exceptions and execution are separate, future contracts.
template <typename Command>
class RuntimeCommandPolicyGate {
public:
    using StatusReader = bool (*)(const void*, System::RuntimeStatus&);
    using ClassResolver = bool (*)(const Command&, const void*, CommandClass&);

    RuntimeCommandPolicyGate(
        StatusReader readStatus, const void* statusContext,
        ClassResolver resolveClass, const void* resolverContext,
        CommandClass routeClass
    ) : readStatus_(readStatus), statusContext_(statusContext),
        resolveClass_(resolveClass), resolverContext_(resolverContext),
        routeClass_(routeClass) {
    }

    RuntimeCommandPolicyGate(const RuntimeCommandPolicyGate&) = delete;
    RuntimeCommandPolicyGate& operator=(const RuntimeCommandPolicyGate&) = delete;

    bool allows(const Command& command) const {
        if (readStatus_ == nullptr || resolveClass_ == nullptr) {
            return false;
        }
        System::RuntimeStatus status {};
        if (!readStatus_(statusContext_, status)) {
            return false;
        }
        CommandClass commandClass = CommandClass::NormalDomain;
        if (!resolveClass_(command, resolverContext_, commandClass) ||
            commandClass != routeClass_) {
            return false;
        }
        switch (commandClass) {
        case CommandClass::NormalDomain:
            return status.operational == System::OperationalState::RUNNING;
        case CommandClass::SystemRecovery:
            return status.operational == System::OperationalState::RUNNING ||
                   status.operational == System::OperationalState::ERROR;
        default:
            return false;
        }
    }

    static bool callback(const Command& command, void* context) {
        return context != nullptr &&
               static_cast<const RuntimeCommandPolicyGate*>(context)->allows(command);
    }

private:
    StatusReader readStatus_;
    const void* statusContext_;
    ClassResolver resolveClass_;
    const void* resolverContext_;
    CommandClass routeClass_;
};

} // namespace Commands
} // namespace AquaCore