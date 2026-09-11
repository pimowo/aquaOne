#pragma once

#include <stdint.h>

#include "AquaCore/System/RestartReason.h"

namespace AquaCore {

class SystemBackend {
public:
    virtual ~SystemBackend() = default;

    virtual uint32_t uptimeMs() const = 0;
    virtual RestartReason restartReason() const = 0;
};

} // namespace AquaCore