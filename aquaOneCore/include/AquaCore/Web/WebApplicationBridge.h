#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "AquaCore/Web/ActionBridgeSynchronizer.h"

namespace AquaCore {
namespace Web {

struct ApplicationBridgeToken {
    size_t slotIndex = 0U;
    uint64_t generation = 0U;
};

enum class ApplicationBridgeSubmitResult : uint8_t {
    Accepted,
    QueueFull,
    SynchronizationFailure
};

enum class ApplicationBridgeWaitResult : uint8_t {
    Completed,
    TimedOutAccepted,
    InvalidToken,
    SynchronizationFailure
};

enum class ApplicationBridgeAbandonResult : uint8_t {
    Abandoned,
    InvalidToken,
    SynchronizationFailure
};

// Fixed-capacity bridge for serialized Application workflows which are not
// backed by CommandPipeline. Request and Result are copied values. Trivial
// copyability cannot reject nested pointers, so callers must also keep both
// types bounded, self-contained and semantically pointer-free.
template <typename Request, typename Result, size_t Capacity>
class WebApplicationBridge {
    static_assert(Capacity > 0U, "Application bridge needs at least one slot");
    static_assert(std::is_trivially_copyable<Request>::value,
                  "Application request must be trivially copyable");
    static_assert(std::is_trivially_copyable<Result>::value,
                  "Application result must be trivially copyable");
    static_assert(!std::is_pointer<Request>::value,
                  "Application request must be self-contained");
    static_assert(!std::is_pointer<Result>::value,
                  "Application result must be self-contained");

public:
    using Executor = Result (*)(const Request& request, void* context);

    explicit WebApplicationBridge(ActionBridgeSynchronizer& synchronizer)
        : synchronizer_(synchronizer) {}
    WebApplicationBridge(const WebApplicationBridge&) = delete;
    WebApplicationBridge& operator=(const WebApplicationBridge&) = delete;

    bool isCapacitySupported() const {
        return synchronizer_.completionSlotCapacity() >= Capacity;
    }

    ApplicationBridgeSubmitResult submit(
        const Request& request, ApplicationBridgeToken& token
    ) {
        if (!isCapacitySupported() || !synchronizer_.lock()) {
            return ApplicationBridgeSubmitResult::SynchronizationFailure;
        }
        const size_t slotIndex = freeSlot();
        if (slotIndex == Capacity) {
            synchronizer_.unlock();
            return ApplicationBridgeSubmitResult::QueueFull;
        }
        if (!synchronizer_.prepareCompletion(slotIndex)) {
            synchronizer_.unlock();
            return ApplicationBridgeSubmitResult::SynchronizationFailure;
        }
        Slot& slot = slots_[slotIndex];
        slot.generation = nextGeneration(slot.generation);
        slot.request = request;
        slot.state = State::Queued;
        slot.waiterAttached = true;
        pending_[pendingTail_] = slotIndex;
        pendingTail_ = (pendingTail_ + 1U) % Capacity;
        ++pendingCount_;
        token.slotIndex = slotIndex;
        token.generation = slot.generation;
        synchronizer_.unlock();
        return ApplicationBridgeSubmitResult::Accepted;
    }

    // Application context only. Executes at most one accepted request and
    // never calls the executor while holding the metadata lock.
    bool processOne(Executor executor, void* context = nullptr) {
        if (executor == nullptr || !synchronizer_.lock()) {
            return false;
        }
        if (pendingCount_ == 0U) {
            synchronizer_.unlock();
            return false;
        }
        const size_t slotIndex = pending_[pendingHead_];
        pendingHead_ = (pendingHead_ + 1U) % Capacity;
        --pendingCount_;
        Slot& slot = slots_[slotIndex];
        if (slot.state != State::Queued) {
            synchronizer_.unlock();
            return false;
        }
        const Request request = slot.request;
        const uint64_t generation = slot.generation;
        slot.state = State::Processing;
        synchronizer_.unlock();

        const Result result = executor(request, context);

        if (!synchronizer_.lock()) {
            return false;
        }
        Slot& completed = slots_[slotIndex];
        if (completed.generation != generation ||
            completed.state != State::Processing) {
            synchronizer_.unlock();
            return false;
        }
        if (!completed.waiterAttached) {
            release(completed);
            synchronizer_.unlock();
            return true;
        }
        completed.result = result;
        completed.state = State::Completed;
        const bool signaled = synchronizer_.signalCompletion(slotIndex);
        synchronizer_.unlock();
        return signaled;
    }

    ApplicationBridgeWaitResult wait(
        const ApplicationBridgeToken& token,
        uint32_t timeoutMs,
        Result& result
    ) {
        if (!synchronizer_.lock()) {
            return ApplicationBridgeWaitResult::SynchronizationFailure;
        }
        if (!matches(token) || !slots_[token.slotIndex].waiterAttached) {
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::InvalidToken;
        }
        Slot& initial = slots_[token.slotIndex];
        if (initial.state == State::Completed) {
            result = initial.result;
            release(initial);
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::Completed;
        }
        synchronizer_.unlock();

        const ActionBridgeWaitStatus waitStatus =
            synchronizer_.waitForCompletion(token.slotIndex, timeoutMs);

        if (!synchronizer_.lock()) {
            return ApplicationBridgeWaitResult::SynchronizationFailure;
        }
        if (!matches(token) || !slots_[token.slotIndex].waiterAttached) {
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::InvalidToken;
        }
        Slot& slot = slots_[token.slotIndex];
        if (slot.state == State::Completed) {
            result = slot.result;
            release(slot);
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::Completed;
        }
        if (waitStatus == ActionBridgeWaitStatus::Failure ||
            waitStatus == ActionBridgeWaitStatus::Signaled) {
            // The request was already accepted, so a completion-channel
            // failure must not cancel it. Detach the failed waiter and let
            // processOne() reclaim the slot after the Application effect.
            slot.waiterAttached = false;
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::SynchronizationFailure;
        }
        slot.waiterAttached = false;
        if (slot.state == State::Queued || slot.state == State::Processing) {
            synchronizer_.unlock();
            return ApplicationBridgeWaitResult::TimedOutAccepted;
        }
        synchronizer_.unlock();
        return ApplicationBridgeWaitResult::InvalidToken;
    }

    ApplicationBridgeAbandonResult abandon(
        const ApplicationBridgeToken& token
    ) {
        if (!synchronizer_.lock()) {
            return ApplicationBridgeAbandonResult::SynchronizationFailure;
        }
        if (!matches(token) || !slots_[token.slotIndex].waiterAttached) {
            synchronizer_.unlock();
            return ApplicationBridgeAbandonResult::InvalidToken;
        }
        Slot& slot = slots_[token.slotIndex];
        if (slot.state == State::Completed) {
            release(slot);
        } else {
            slot.waiterAttached = false;
        }
        synchronizer_.unlock();
        return ApplicationBridgeAbandonResult::Abandoned;
    }

    size_t pendingCount() const {
        if (!synchronizer_.lock()) {
            return 0U;
        }
        const size_t count = pendingCount_;
        synchronizer_.unlock();
        return count;
    }

private:
    enum class State : uint8_t { Free, Queued, Processing, Completed };
    struct Slot {
        Request request {};
        Result result {};
        uint64_t generation = 0U;
        State state = State::Free;
        bool waiterAttached = false;
    };

    size_t freeSlot() const {
        for (size_t index = 0U; index < Capacity; ++index) {
            if (slots_[index].state == State::Free) return index;
        }
        return Capacity;
    }
    bool matches(const ApplicationBridgeToken& token) const {
        return token.slotIndex < Capacity &&
            slots_[token.slotIndex].state != State::Free &&
            slots_[token.slotIndex].generation == token.generation;
    }
    static uint64_t nextGeneration(uint64_t generation) {
        ++generation;
        return generation == 0U ? 1U : generation;
    }
    static void release(Slot& slot) {
        slot.waiterAttached = false;
        slot.state = State::Free;
    }

    ActionBridgeSynchronizer& synchronizer_;
    Slot slots_[Capacity] {};
    size_t pending_[Capacity] {};
    size_t pendingHead_ = 0U;
    size_t pendingTail_ = 0U;
    size_t pendingCount_ = 0U;
};

} // namespace Web
} // namespace AquaCore
