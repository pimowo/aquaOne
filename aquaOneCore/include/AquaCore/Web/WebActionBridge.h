#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "AquaCore/Commands/CommandPipeline.h"
#include "AquaCore/Web/ActionBridgeSynchronizer.h"

namespace AquaCore {
namespace Web {

struct ActionBridgeToken {
    size_t slotIndex = 0U;
    uint64_t generation = 0U;
};

enum class ActionBridgeSubmitResult : uint8_t {
    Accepted,
    QueueFull,
    SynchronizationFailure
};

enum class ActionBridgeWaitResult : uint8_t {
    Completed,
    TimedOutAccepted,
    InvalidToken,
    SynchronizationFailure
};

enum class ActionBridgeAbandonResult : uint8_t {
    Abandoned,
    InvalidToken,
    SynchronizationFailure
};

// Fixed-capacity task-to-Application command bridge. Command is copied into
// bridge-owned storage on acceptance. Command must be bounded and
// self-contained: trivially copyable alone cannot reject nested pointers,
// borrowed views, dynamic ownership, transport handles or Domain references.
// A producer must call exactly one of wait() or abandon() for every accepted
// token. Tokens are runtime-local and must be discarded after that protocol.
template <typename Command, size_t Capacity>
class WebActionBridge {
    static_assert(Capacity > 0U, "Web action bridge needs at least one slot");
    static_assert(std::is_trivially_copyable<Command>::value,
                  "Web command must be trivially copyable");
    static_assert(!std::is_pointer<Command>::value,
                  "Web command must be a self-contained value");

public:
    WebActionBridge(
        ActionBridgeSynchronizer& synchronizer,
        const Commands::CommandPipeline<Command>& pipeline
    ) : synchronizer_(synchronizer), pipeline_(pipeline) {}
    WebActionBridge(const WebActionBridge&) = delete;
    WebActionBridge& operator=(const WebActionBridge&) = delete;

    bool isCapacitySupported() const {
        return synchronizer_.completionSlotCapacity() >= Capacity;
    }

    ActionBridgeSubmitResult submit(const Command& command,
                                    ActionBridgeToken& token) {
        if (!isCapacitySupported()) {
            return ActionBridgeSubmitResult::SynchronizationFailure;
        }
        if (!synchronizer_.lock()) {
            return ActionBridgeSubmitResult::SynchronizationFailure;
        }
        const size_t slotIndex = freeSlot();
        if (slotIndex == Capacity) {
            synchronizer_.unlock();
            return ActionBridgeSubmitResult::QueueFull;
        }
        if (!synchronizer_.prepareCompletion(slotIndex)) {
            synchronizer_.unlock();
            return ActionBridgeSubmitResult::SynchronizationFailure;
        }
        Slot& slot = slots_[slotIndex];
        slot.generation = nextGeneration(slot.generation);
        slot.command = command;
        slot.state = State::Queued;
        slot.waiterAttached = true;
        pending_[pendingTail_] = slotIndex;
        pendingTail_ = (pendingTail_ + 1U) % Capacity;
        ++pendingCount_;
        token.slotIndex = slotIndex;
        token.generation = slot.generation;
        synchronizer_.unlock();
        return ActionBridgeSubmitResult::Accepted;
    }

    // Application context only. Executes at most one accepted command.
    bool processOne() {
        if (!synchronizer_.lock()) {
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
        const Command command = slot.command;
        const uint64_t generation = slot.generation;
        slot.state = State::Processing;
        synchronizer_.unlock();

        // Pipeline callbacks execute without the bridge metadata lock.
        const Commands::CommandExecutionResult result = pipeline_.execute(command);

        if (!synchronizer_.lock()) {
            // A concrete synchronizer must not fail after an accepted command
            // has entered Application processing. The result cannot be safely
            // published if this invariant is violated.
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
        // Signal before unlock so an old completion cannot arrive after a
        // waiter has consumed/reused this slot.
        const bool signaled = synchronizer_.signalCompletion(slotIndex);
        synchronizer_.unlock();
        // State remains authoritative, but the caller is told that waking a
        // currently waiting producer failed.
        return signaled;
    }

    ActionBridgeWaitResult wait(const ActionBridgeToken& token,
                                uint32_t timeoutMs,
                                Commands::CommandExecutionResult& result) {
        if (!synchronizer_.lock()) {
            return ActionBridgeWaitResult::SynchronizationFailure;
        }
        if (!matches(token)) {
            synchronizer_.unlock();
            return ActionBridgeWaitResult::InvalidToken;
        }
        Slot& initial = slots_[token.slotIndex];
        if (!initial.waiterAttached) {
            synchronizer_.unlock();
            return ActionBridgeWaitResult::InvalidToken;
        }
        if (initial.state == State::Completed) {
            result = initial.result;
            release(initial);
            synchronizer_.unlock();
            return ActionBridgeWaitResult::Completed;
        }
        synchronizer_.unlock();

        const ActionBridgeWaitStatus waitStatus =
            synchronizer_.waitForCompletion(token.slotIndex, timeoutMs);

        if (!synchronizer_.lock()) {
            return ActionBridgeWaitResult::SynchronizationFailure;
        }
        if (!matches(token) || !slots_[token.slotIndex].waiterAttached) {
            synchronizer_.unlock();
            return ActionBridgeWaitResult::InvalidToken;
        }
        Slot& slot = slots_[token.slotIndex];
        // This final check resolves completion-versus-timeout races.
        if (slot.state == State::Completed) {
            result = slot.result;
            release(slot);
            synchronizer_.unlock();
            return ActionBridgeWaitResult::Completed;
        }
        if (waitStatus == ActionBridgeWaitStatus::Failure ||
            waitStatus == ActionBridgeWaitStatus::Signaled) {
            // Do not detach the waiter on an anomalous wake or platform
            // failure: it may retry wait() or explicitly abandon the token.
            synchronizer_.unlock();
            return ActionBridgeWaitResult::SynchronizationFailure;
        }
        slot.waiterAttached = false;
        if (slot.state == State::Queued || slot.state == State::Processing) {
            synchronizer_.unlock();
            return ActionBridgeWaitResult::TimedOutAccepted;
        }
        synchronizer_.unlock();
        return ActionBridgeWaitResult::InvalidToken;
    }

    ActionBridgeAbandonResult abandon(const ActionBridgeToken& token) {
        if (!synchronizer_.lock()) {
            return ActionBridgeAbandonResult::SynchronizationFailure;
        }
        if (!matches(token) || !slots_[token.slotIndex].waiterAttached) {
            synchronizer_.unlock();
            return ActionBridgeAbandonResult::InvalidToken;
        }
        Slot& slot = slots_[token.slotIndex];
        if (slot.state == State::Completed) {
            release(slot);
        } else {
            slot.waiterAttached = false;
        }
        synchronizer_.unlock();
        return ActionBridgeAbandonResult::Abandoned;
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
        Command command {};
        Commands::CommandExecutionResult result =
            Commands::CommandExecutionResult::invalidPipeline();
        uint64_t generation = 0U;
        State state = State::Free;
        bool waiterAttached = false;
    };

    size_t freeSlot() const {
        for (size_t index = 0U; index < Capacity; ++index) {
            if (slots_[index].state == State::Free) {
                return index;
            }
        }
        return Capacity;
    }
    bool matches(const ActionBridgeToken& token) const {
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
    const Commands::CommandPipeline<Command>& pipeline_;
    Slot slots_[Capacity] {};
    size_t pending_[Capacity] {};
    size_t pendingHead_ = 0U;
    size_t pendingTail_ = 0U;
    size_t pendingCount_ = 0U;
};

} // namespace Web
} // namespace AquaCore
