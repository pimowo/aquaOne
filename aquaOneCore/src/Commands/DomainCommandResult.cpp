#include "AquaCore/Commands/DomainCommandResult.h"

namespace AquaCore {
namespace Commands {

const char* domainCommandResultName(DomainCommandResult result) {
    switch (result) {
        case DomainCommandResult::Completed:
            return "COMPLETED";
        case DomainCommandResult::Rejected:
            return "REJECTED";
        case DomainCommandResult::InvalidState:
            return "INVALID_STATE";
        case DomainCommandResult::OperationStarted:
            return "OPERATION_STARTED";
        default:
            return "UNKNOWN";
    }
}

} // namespace Commands
} // namespace AquaCore
