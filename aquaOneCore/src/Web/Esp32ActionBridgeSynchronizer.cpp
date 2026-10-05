#include "AquaCore/Web/Esp32ActionBridgeSynchronizer.h"

namespace AquaCore {
namespace Web {

Esp32ActionBridgeSynchronizer::Esp32ActionBridgeSynchronizer()
    : mutex_(xSemaphoreCreateMutexStatic(&mutexStorage_)) {
    for (size_t index = 0U; index < MAX_SLOTS; ++index) {
        completions_[index] = xSemaphoreCreateBinaryStatic(&completionStorage_[index]);
    }
}

Esp32ActionBridgeSynchronizer::~Esp32ActionBridgeSynchronizer() {
    for (size_t index = 0U; index < MAX_SLOTS; ++index) {
        if (completions_[index] != nullptr) {
            vSemaphoreDelete(completions_[index]);
        }
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

bool Esp32ActionBridgeSynchronizer::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void Esp32ActionBridgeSynchronizer::unlock() {
    configASSERT(mutex_ != nullptr);
    const BaseType_t given = xSemaphoreGive(mutex_);
    configASSERT(given == pdTRUE);
    (void)given;
}

bool Esp32ActionBridgeSynchronizer::prepareCompletion(size_t slotIndex) {
    if (slotIndex >= MAX_SLOTS || completions_[slotIndex] == nullptr) {
        return false;
    }
    // A binary semaphore can hold at most one stale completion notification.
    (void)xSemaphoreTake(completions_[slotIndex], 0U);
    return true;
}

ActionBridgeWaitStatus Esp32ActionBridgeSynchronizer::waitForCompletion(
    size_t slotIndex, uint32_t timeoutMs
) {
    if (slotIndex >= MAX_SLOTS || completions_[slotIndex] == nullptr) {
        return ActionBridgeWaitStatus::Failure;
    }
    return xSemaphoreTake(completions_[slotIndex], timeoutTicks(timeoutMs)) == pdTRUE
        ? ActionBridgeWaitStatus::Signaled
        : ActionBridgeWaitStatus::TimedOut;
}

bool Esp32ActionBridgeSynchronizer::signalCompletion(size_t slotIndex) {
    return slotIndex < MAX_SLOTS && completions_[slotIndex] != nullptr &&
        xSemaphoreGive(completions_[slotIndex]) == pdTRUE;
}

TickType_t Esp32ActionBridgeSynchronizer::timeoutTicks(uint32_t timeoutMs) {
    if (timeoutMs == 0U) {
        return 0U;
    }
    const uint64_t ticks =
        (static_cast<uint64_t>(timeoutMs) + portTICK_PERIOD_MS - 1U) /
        portTICK_PERIOD_MS;
    const uint64_t maximum = static_cast<uint64_t>(portMAX_DELAY) - 1U;
    return static_cast<TickType_t>(ticks > maximum ? maximum : ticks);
}

} // namespace Web
} // namespace AquaCore
