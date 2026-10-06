#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <type_traits>

#include "AquaCore/System/RuntimeIdentity.h"
#include "AquaCore/Web/Realtime.h"

namespace AquaCore {
namespace Web {

enum class RealtimeSnapshotCoherence : uint8_t {
    Unavailable,
    Incoherent,
    CoherentCurrent
};

// Metadata and payload cross the publication boundary as one bounded value.
template <typename T>
class RealtimeSnapshot {
    static_assert(std::is_trivially_copyable<T>::value,
                  "Realtime snapshot payload must be trivially copyable");
    static_assert(!std::is_pointer<T>::value,
                  "Realtime snapshot payload must be self-contained");

public:
    RealtimeSnapshot()
        : runtime_(), watermark_(),
          coherence_(RealtimeSnapshotCoherence::Unavailable), payload_() {}

    static bool coherent(Identity::RuntimeIdentity runtime,
                         RealtimeStreamPosition watermark,
                         const T& payload,
                         RealtimeSnapshot& output) {
        output = RealtimeSnapshot();
        if (!runtime.isValid() || !watermark.isValid()) {
            return false;
        }
        output.runtime_ = runtime;
        output.watermark_ = watermark;
        output.coherence_ = RealtimeSnapshotCoherence::CoherentCurrent;
        output.payload_ = payload;
        return true;
    }

    bool isCoherentCurrent() const {
        return runtime_.isValid() && watermark_.isValid() &&
            coherence_ == RealtimeSnapshotCoherence::CoherentCurrent;
    }
    Identity::RuntimeIdentity runtimeIdentity() const { return runtime_; }
    RealtimeStreamPosition watermark() const { return watermark_; }
    RealtimeSnapshotCoherence coherence() const { return coherence_; }
    const T& payload() const { return payload_; }

private:
    Identity::RuntimeIdentity runtime_;
    RealtimeStreamPosition watermark_;
    RealtimeSnapshotCoherence coherence_;
    T payload_;
};

class RealtimeStreamStartState {
public:
    RealtimeStreamStartState() : runtime_(), position_(), available_(false) {}

    static bool fromValues(Identity::RuntimeIdentity runtime,
                           RealtimeStreamPosition position,
                           RealtimeStreamStartState& output) {
        output = RealtimeStreamStartState();
        if (!runtime.isValid() || !position.isValid()) {
            return false;
        }
        output.runtime_ = runtime;
        output.position_ = position;
        output.available_ = true;
        return true;
    }

    bool isAvailable() const {
        return available_ && runtime_.isValid() && position_.isValid();
    }
    Identity::RuntimeIdentity runtimeIdentity() const { return runtime_; }
    RealtimeStreamPosition position() const { return position_; }

private:
    Identity::RuntimeIdentity runtime_;
    RealtimeStreamPosition position_;
    bool available_;
};

class RealtimeRecoveryState {
public:
    typedef uint64_t Generation;

    // The generation is a runtime-local technical lifetime token. It is not a
    // stream sequence, snapshot epoch, or durable identity. The top state bit
    // and the generation change atomically so request cannot race past clear.
    void request() {
        uint64_t current = state_.load();
        for (;;) {
            if ((current & REQUIRED_MASK) != 0U) return;
            const Generation generation = current & GENERATION_MASK;
            const Generation next = generation == GENERATION_MASK ?
                generation : generation + 1U;
            const uint64_t requested = REQUIRED_MASK | next;
            if (state_.compare_exchange_weak(current, requested)) return;
        }
    }
    bool isRequired() const { return (state_.load() & REQUIRED_MASK) != 0U; }
    bool allowsFanout() const { return !isRequired(); }
    Generation generation() const { return state_.load() & GENERATION_MASK; }
    bool acceptsGeneration(Generation captured) const {
        const uint64_t state = state_.load();
        const Generation generation = state & GENERATION_MASK;
        return captured != 0U && generation != GENERATION_MASK &&
            (state & REQUIRED_MASK) == 0U && captured == generation;
    }

    bool tryClear(bool noAffectedClients, bool serverReady,
                  const RealtimeStreamStartState& publishedState,
                  Identity::RuntimeIdentity coherentRuntime,
                  RealtimeStreamPosition coherentPosition) {
        uint64_t expected = state_.load();
        if ((expected & REQUIRED_MASK) == 0U ||
            (expected & GENERATION_MASK) == GENERATION_MASK ||
            !noAffectedClients || !serverReady ||
            !publishedState.isAvailable() || !coherentRuntime.isValid() ||
            !coherentPosition.isValid() ||
            publishedState.runtimeIdentity() != coherentRuntime ||
            publishedState.position() != coherentPosition) {
            return false;
        }
        return state_.compare_exchange_strong(expected,
                                               expected & GENERATION_MASK);
    }

private:
    static constexpr uint64_t REQUIRED_MASK = UINT64_C(1) << 63U;
    static constexpr uint64_t GENERATION_MASK = REQUIRED_MASK - 1U;
    std::atomic<uint64_t> state_ {1U};
};

struct RealtimeResyncResourceBinding {
    typedef bool (*InvalidateFn)(void* context);
    typedef bool (*BuildFn)(void* context, Identity::RuntimeIdentity runtime,
                            RealtimeStreamPosition position);
    typedef bool (*PublishFn)(void* context);

    RealtimeResyncResourceBinding() = default;
    RealtimeResyncResourceBinding(void* contextValue,
                                  InvalidateFn invalidateValue,
                                  BuildFn buildValue,
                                  PublishFn publishValue)
        : context(contextValue), invalidate(invalidateValue),
          build(buildValue), publish(publishValue) {}

    void* context = nullptr;
    InvalidateFn invalidate = nullptr;
    BuildFn build = nullptr;
    PublishFn publish = nullptr;

    bool isValid() const {
        return invalidate != nullptr && build != nullptr && publish != nullptr;
    }
};

enum class RealtimeCohortPublicationResult : uint8_t {
    Success,
    InvalidComposition,
    SequenceFailure,
    ResourceFailure,
    StreamStateFailure,
    NotificationFailure
};

// Application-side, serialized coordinator. The binding array, each borrowed
// context, and every publication target must outlive this publisher. Builders
// read authoritative state and must not mutate Domain. Callbacks are static
// composition bindings and must not call Domain while holding a Web transport
// mutex. A PublishedSnapshot read is coherent-current only when the outer read
// succeeds and the returned RealtimeSnapshot::isCoherentCurrent() is true.
template <size_t ResourceCapacity>
class RealtimeCohortPublisher {
public:
    typedef bool (*PublishStreamStateFn)(void* context,
                                         const RealtimeStreamStartState& state);
    typedef RealtimePublicationResult (*SubmitNotificationFn)(
        void* context, const RealtimeNotificationMetadata& metadata);
    typedef void (*RequestRecoveryFn)(void* context);

    RealtimeCohortPublisher(
        Identity::RuntimeIdentity runtime,
        RealtimeStreamSequencer& sequencer,
        const RealtimeResyncResourceBinding* resources,
        size_t resourceCount,
        PublishStreamStateFn publishStreamState,
        void* streamStateContext,
        SubmitNotificationFn submitNotification,
        void* notificationContext,
        RequestRecoveryFn requestRecovery,
        void* recoveryContext
    ) : runtime_(runtime), sequencer_(sequencer), resources_(resources),
        resourceCount_(resourceCount), publishStreamState_(publishStreamState),
        streamStateContext_(streamStateContext),
        submitNotification_(submitNotification),
        notificationContext_(notificationContext),
        requestRecovery_(requestRecovery), recoveryContext_(recoveryContext),
        coherent_(false), coherentPosition_() {}

    RealtimeCohortPublicationResult publishBaseline() {
        return publishAt(RealtimeStreamPosition::beforeFirst(), nullptr);
    }

    RealtimeCohortPublicationResult publishTransition() {
        RealtimeNotificationMetadata metadata;
        if (sequencer_.issue(metadata) != RealtimeSequenceIssueResult::Success) {
            requestRecovery();
            return RealtimeCohortPublicationResult::SequenceFailure;
        }
        if (metadata.runtimeIdentity() != runtime_) {
            requestRecovery();
            return RealtimeCohortPublicationResult::SequenceFailure;
        }
        RealtimeStreamPosition position;
        RealtimeStreamPosition::at(metadata.sequence(), position);
        return publishAt(position, &metadata);
    }

    RealtimeCohortPublicationResult republishCurrentCohort() {
        return publishAt(sequencer_.currentPosition(), nullptr);
    }

    bool isCoherentAt(Identity::RuntimeIdentity runtime,
                      RealtimeStreamPosition position) const {
        return coherent_ && runtime_ == runtime && coherentPosition_ == position;
    }
    RealtimeStreamPosition coherentPosition() const {
        return coherentPosition_;
    }

private:
    bool compositionIsValid() const {
        if (!runtime_.isValid() || sequencer_.runtimeIdentity() != runtime_ ||
            resources_ == nullptr ||
            resourceCount_ == 0U || resourceCount_ > ResourceCapacity ||
            publishStreamState_ == nullptr || requestRecovery_ == nullptr) {
            return false;
        }
        for (size_t i = 0U; i < resourceCount_; ++i) {
            if (!resources_[i].isValid()) return false;
            for (size_t j = i + 1U; j < resourceCount_; ++j) {
                if (resources_[i].context == resources_[j].context &&
                    resources_[i].invalidate == resources_[j].invalidate &&
                    resources_[i].build == resources_[j].build &&
                    resources_[i].publish == resources_[j].publish) {
                    return false;
                }
            }
        }
        return true;
    }

    void invalidateAll() {
        for (size_t i = 0U; i < resourceCount_; ++i) {
            (void)resources_[i].invalidate(resources_[i].context);
        }
    }

    RealtimeCohortPublicationResult publishAt(
        RealtimeStreamPosition position,
        const RealtimeNotificationMetadata* notification
    ) {
        coherent_ = false;
        coherentPosition_ = RealtimeStreamPosition();
        if (!compositionIsValid() || !position.isValid()) {
            requestRecovery();
            return RealtimeCohortPublicationResult::InvalidComposition;
        }
        for (size_t i = 0U; i < resourceCount_; ++i) {
            if (!resources_[i].invalidate(resources_[i].context)) {
                invalidateAll();
                requestRecovery();
                return RealtimeCohortPublicationResult::ResourceFailure;
            }
        }
        for (size_t i = 0U; i < resourceCount_; ++i) {
            if (!resources_[i].build(resources_[i].context, runtime_, position)) {
                invalidateAll();
                requestRecovery();
                return RealtimeCohortPublicationResult::ResourceFailure;
            }
        }
        for (size_t i = 0U; i < resourceCount_; ++i) {
            if (!resources_[i].publish(resources_[i].context)) {
                invalidateAll();
                requestRecovery();
                return RealtimeCohortPublicationResult::ResourceFailure;
            }
        }
        RealtimeStreamStartState state;
        RealtimeStreamStartState::fromValues(runtime_, position, state);
        if (!publishStreamState_(streamStateContext_, state)) {
            invalidateAll();
            requestRecovery();
            return RealtimeCohortPublicationResult::StreamStateFailure;
        }
        coherent_ = true;
        coherentPosition_ = position;
        if (notification != nullptr) {
            if (submitNotification_ == nullptr ||
                submitNotification_(notificationContext_, *notification) !=
                    RealtimePublicationResult::Accepted) {
                requestRecovery();
                return RealtimeCohortPublicationResult::NotificationFailure;
            }
        }
        return RealtimeCohortPublicationResult::Success;
    }

    void requestRecovery() {
        if (requestRecovery_ != nullptr) requestRecovery_(recoveryContext_);
    }

    Identity::RuntimeIdentity runtime_;
    RealtimeStreamSequencer& sequencer_;
    const RealtimeResyncResourceBinding* resources_;
    size_t resourceCount_;
    PublishStreamStateFn publishStreamState_;
    void* streamStateContext_;
    SubmitNotificationFn submitNotification_;
    void* notificationContext_;
    RequestRecoveryFn requestRecovery_;
    void* recoveryContext_;
    bool coherent_;
    RealtimeStreamPosition coherentPosition_;
};

enum class RealtimeClientState : uint8_t {
    Free,
    Connecting,
    Live,
    Closing
};

struct RealtimeClientToken {
    RealtimeClientToken() = default;
    RealtimeClientToken(size_t slotValue, uint64_t generationValue)
        : slot(slotValue), generation(generationValue) {}
    size_t slot = 0U;
    uint64_t generation = 0U;
    bool isValid() const { return generation != 0U; }
};

template <size_t Capacity>
class RealtimeClientRegistry {
public:
    struct Slot {
        RealtimeClientState state = RealtimeClientState::Free;
        int fd = -1;
        uint64_t generation = 0U;
        RealtimeStreamPosition capturedPosition {};
        bool missedWhileConnecting = false;
        bool closeRequested = false;
    };

    bool connect(int fd, RealtimeStreamPosition position,
                 RealtimeClientToken& token) {
        token = RealtimeClientToken();
        if (fd < 0 || !position.isValid()) return false;
        for (size_t i = 0U; i < Capacity; ++i) {
            if (slots_[i].state != RealtimeClientState::Free) continue;
            ++nextGeneration_;
            if (nextGeneration_ == 0U) ++nextGeneration_;
            slots_[i].state = RealtimeClientState::Connecting;
            slots_[i].fd = fd;
            slots_[i].generation = nextGeneration_;
            slots_[i].capturedPosition = position;
            slots_[i].missedWhileConnecting = false;
            slots_[i].closeRequested = false;
            token.slot = i;
            token.generation = nextGeneration_;
            return true;
        }
        return false;
    }

    bool isTokenCurrent(RealtimeClientToken token) const {
        return token.isValid() && token.slot < Capacity &&
            slots_[token.slot].state != RealtimeClientState::Free &&
            slots_[token.slot].generation == token.generation;
    }

    bool isTokenCurrentForFd(RealtimeClientToken token, int fd) const {
        return isTokenCurrent(token) && slots_[token.slot].fd == fd;
    }

    void notePublication(RealtimeStreamPosition position) {
        if (!position.isValid()) return;
        for (size_t i = 0U; i < Capacity; ++i) {
            Slot& slot = slots_[i];
            int ordering = 0;
            if (slot.state == RealtimeClientState::Connecting &&
                position.compare(slot.capturedPosition, ordering) &&
                ordering > 0) {
                slot.missedWhileConnecting = true;
            }
        }
    }

    bool markerSucceeded(RealtimeClientToken token, bool globalRecovery) {
        if (!isTokenCurrent(token)) return false;
        Slot& slot = slots_[token.slot];
        if (slot.state != RealtimeClientState::Connecting || globalRecovery ||
            slot.missedWhileConnecting) {
            slot.state = RealtimeClientState::Closing;
            return false;
        }
        slot.state = RealtimeClientState::Live;
        return true;
    }

    void markerFailed(RealtimeClientToken token) {
        if (isTokenCurrent(token)) slots_[token.slot].state = RealtimeClientState::Closing;
    }
    void sendFailed(RealtimeClientToken token) { markerFailed(token); }
    bool markCloseRequested(RealtimeClientToken token, int fd) {
        if (!isTokenCurrentForFd(token, fd) ||
            slots_[token.slot].state != RealtimeClientState::Closing ||
            slots_[token.slot].closeRequested) {
            return false;
        }
        slots_[token.slot].closeRequested = true;
        return true;
    }
    bool reclaimInactive(RealtimeClientToken token, int fd) {
        if (!isTokenCurrentForFd(token, fd)) return false;
        release(token);
        return true;
    }
    void requestRecovery() {
        for (size_t i = 0U; i < Capacity; ++i) {
            if (slots_[i].state != RealtimeClientState::Free) {
                slots_[i].state = RealtimeClientState::Closing;
            }
        }
    }
    void release(RealtimeClientToken token) {
        if (!isTokenCurrent(token)) return;
        const uint64_t generation = slots_[token.slot].generation;
        slots_[token.slot] = Slot();
        slots_[token.slot].generation = generation;
    }
    bool hasClients() const {
        for (size_t i = 0U; i < Capacity; ++i) {
            if (slots_[i].state != RealtimeClientState::Free) return true;
        }
        return false;
    }
    void reset() {
        for (size_t i = 0U; i < Capacity; ++i) slots_[i] = Slot();
    }
    Slot* slot(size_t index) { return index < Capacity ? &slots_[index] : nullptr; }
    const Slot* slot(size_t index) const { return index < Capacity ? &slots_[index] : nullptr; }

private:
    Slot slots_[Capacity] {};
    uint64_t nextGeneration_ = 0U;
};

} // namespace Web
} // namespace AquaCore
