#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Commands {

enum class DomainCommandResult : uint8_t {
    Completed,
    Rejected,
    InvalidState,
    OperationStarted
};

const char* domainCommandResultName(DomainCommandResult result);

} // namespace Commands
} // namespace AquaCore
