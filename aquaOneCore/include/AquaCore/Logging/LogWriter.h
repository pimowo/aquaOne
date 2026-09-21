#pragma once

#include "AquaCore/Logging/LogLevel.h"

namespace AquaCore {

// Narrow, best-effort logging capability for consumers such as Domain.
// The owner must outlive every consumer holding a borrowed LogWriter reference.
class LogWriter {
public:
    // Inputs are borrowed and must remain valid for the duration of the call.
    // Delivery is not guaranteed and does not produce a semantic result.
    virtual void write(
        LogLevel level,
        const char* module,
        const char* message
    ) = 0;
};

} // namespace AquaCore