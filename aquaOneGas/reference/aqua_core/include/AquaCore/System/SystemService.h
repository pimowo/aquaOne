#pragma once

#include <stdint.h>

#include "AquaCore/System/DeviceIdentity.h"
#include "AquaCore/System/RestartReason.h"
#include "AquaCore/System/SystemBackend.h"
#include "AquaCore/Version.h"

namespace AquaCore {

enum class SystemState : uint8_t {
    NotStarted,
    Ready
};

struct SystemStatus {
    SystemState state;
    RestartReason restartReason;
    uint32_t uptimeMs;
};

class SystemService {
public:
    SystemService();
    explicit SystemService(SystemBackend& backend);

    bool begin(const DeviceIdentity& identity);

    bool isReady() const;
    uint32_t uptimeMs() const;
    RestartReason restartReason() const;
    const DeviceIdentity& deviceIdentity() const;
    const char* aquaCoreVersion() const;
    SystemStatus status() const;

private:
    SystemBackend* backend_;
    DeviceIdentity identity_;
    RestartReason restartReason_;
    SystemState state_;
};

} // namespace AquaCore