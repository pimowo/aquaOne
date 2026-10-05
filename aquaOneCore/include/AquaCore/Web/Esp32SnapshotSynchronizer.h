#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "AquaCore/Web/SnapshotSynchronizer.h"

namespace AquaCore {
namespace Web {

// Composition-owned static FreeRTOS mutex. Use only from task context, never
// ISR. Destruction requires all publication readers/writers to have stopped.
class Esp32SnapshotSynchronizer final : public SnapshotSynchronizer {
public:
    Esp32SnapshotSynchronizer();
    ~Esp32SnapshotSynchronizer();
    Esp32SnapshotSynchronizer(const Esp32SnapshotSynchronizer&) = delete;
    Esp32SnapshotSynchronizer& operator=(const Esp32SnapshotSynchronizer&) = delete;

    bool isReady() const { return handle_ != nullptr; }
    bool lock() override;
    void unlock() override;

private:
    StaticSemaphore_t storage_ {};
    SemaphoreHandle_t handle_ = nullptr;
};

} // namespace Web
} // namespace AquaCore
