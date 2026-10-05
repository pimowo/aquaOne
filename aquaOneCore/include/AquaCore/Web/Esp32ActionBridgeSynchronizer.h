#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "AquaCore/Web/ActionBridgeSynchronizer.h"

namespace AquaCore {
namespace Web {

class Esp32ActionBridgeSynchronizer final : public ActionBridgeSynchronizer {
public:
    static constexpr size_t MAX_SLOTS = 4U;

    Esp32ActionBridgeSynchronizer();
    ~Esp32ActionBridgeSynchronizer();
    Esp32ActionBridgeSynchronizer(const Esp32ActionBridgeSynchronizer&) = delete;
    Esp32ActionBridgeSynchronizer& operator=(const Esp32ActionBridgeSynchronizer&) = delete;

    bool isReady() const { return mutex_ != nullptr; }
    bool lock() override;
    void unlock() override;
    size_t completionSlotCapacity() const override { return MAX_SLOTS; }
    bool prepareCompletion(size_t slotIndex) override;
    ActionBridgeWaitStatus waitForCompletion(
        size_t slotIndex, uint32_t timeoutMs
    ) override;
    bool signalCompletion(size_t slotIndex) override;

private:
    StaticSemaphore_t mutexStorage_ {};
    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t completionStorage_[MAX_SLOTS] {};
    SemaphoreHandle_t completions_[MAX_SLOTS] {};

    static TickType_t timeoutTicks(uint32_t timeoutMs);
};

} // namespace Web
} // namespace AquaCore
