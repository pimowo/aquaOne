#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"

namespace AquaCore {
namespace Web {

Esp32SnapshotSynchronizer::Esp32SnapshotSynchronizer()
    : handle_(xSemaphoreCreateMutexStatic(&storage_)) {}

Esp32SnapshotSynchronizer::~Esp32SnapshotSynchronizer() {
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
    }
}

bool Esp32SnapshotSynchronizer::lock() {
    return handle_ != nullptr &&
        xSemaphoreTake(handle_, portMAX_DELAY) == pdTRUE;
}

void Esp32SnapshotSynchronizer::unlock() {
    // Only a successful lock may call unlock, from that same task.
    configASSERT(handle_ != nullptr);
    const BaseType_t given = xSemaphoreGive(handle_);
    configASSERT(given == pdTRUE);
    (void)given;
}

} // namespace Web
} // namespace AquaCore
