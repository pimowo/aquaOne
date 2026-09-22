#include "AquaCore/Commands/DomainCommandResult.h"

namespace AquaCore {
namespace Commands {

bool isDomainCommandResultValid(DomainCommandResult result) {
    switch (result) {
        case DomainCommandResult::Completed:
        case DomainCommandResult::Rejected:
        case DomainCommandResult::InvalidState:
        case DomainCommandResult::OperationStarted:
            return true;
        default:
            return false;
    }
}

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
