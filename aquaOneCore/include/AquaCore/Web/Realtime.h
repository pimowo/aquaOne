#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/System/RuntimeIdentity.h"

namespace AquaCore {
namespace Web {

class RealtimeStreamSequence {
public:
    RealtimeStreamSequence() : value_(0U) {}
    static bool fromValue(uint64_t value, RealtimeStreamSequence& output) {
        output = RealtimeStreamSequence(value);
        return output.isValid();
    }
    bool isValid() const { return value_ != 0U; }
    uint64_t value() const { return value_; }
private:
    explicit RealtimeStreamSequence(uint64_t value) : value_(value) {}
    uint64_t value_;
    friend class RealtimeStreamSequencer;
};

enum class RealtimeSequenceIssueResult : uint8_t {
    Success,
    InvalidRuntimeIdentity,
    Exhausted
};

class RealtimeNotificationMetadata {
public:
    RealtimeNotificationMetadata() : runtime_(), sequence_() {}
    static bool fromValues(Identity::RuntimeIdentity runtime,
                           RealtimeStreamSequence sequence,
                           RealtimeNotificationMetadata& output) {
        output = RealtimeNotificationMetadata();
        if (!runtime.isValid() || !sequence.isValid()) {
            return false;
        }
        output = RealtimeNotificationMetadata(runtime, sequence);
        return true;
    }
    bool isValid() const { return runtime_.isValid() && sequence_.isValid(); }
    Identity::RuntimeIdentity runtimeIdentity() const { return runtime_; }
    RealtimeStreamSequence sequence() const { return sequence_; }
private:
    RealtimeNotificationMetadata(Identity::RuntimeIdentity runtime, RealtimeStreamSequence sequence)
        : runtime_(runtime), sequence_(sequence) {}
    Identity::RuntimeIdentity runtime_;
    RealtimeStreamSequence sequence_;
};

class RealtimeStreamSequencer {
public:
    explicit RealtimeStreamSequencer(Identity::RuntimeIdentity runtime)
        : runtime_(runtime), last_(0U) {}
    RealtimeSequenceIssueResult issue(RealtimeNotificationMetadata& output) {
        output = RealtimeNotificationMetadata();
        if (!runtime_.isValid()) {
            return RealtimeSequenceIssueResult::InvalidRuntimeIdentity;
        }
        if (last_ == UINT64_MAX) {
            return RealtimeSequenceIssueResult::Exhausted;
        }
        ++last_;
        RealtimeNotificationMetadata::fromValues(runtime_, RealtimeStreamSequence(last_), output);
        return RealtimeSequenceIssueResult::Success;
    }
private:
    Identity::RuntimeIdentity runtime_;
    uint64_t last_;
    friend class RealtimeStreamSequencerTestAccess;
};

enum class RealtimeFrameType : uint8_t { Text, Binary };
enum class RealtimePublicationResult : uint8_t {
    Accepted,
    NotRunning,
    InvalidMetadata,
    PayloadTooLarge,
    Busy,
    QueueWorkFailure
};

template <size_t SlotCapacity, size_t PayloadCapacity>
class RealtimePublicationPool {
public:
    struct Entry {
        RealtimeNotificationMetadata metadata {};
        RealtimeFrameType type = RealtimeFrameType::Text;
        uint8_t payload[PayloadCapacity] {};
        size_t length = 0U;
        bool inUse = false;
    };

    RealtimePublicationResult acquire(const RealtimeNotificationMetadata& metadata,
                                      RealtimeFrameType type,
                                      const uint8_t* payload, size_t length,
                                      size_t& index) {
        if (!metadata.isValid()) {
            return RealtimePublicationResult::InvalidMetadata;
        }
        if ((payload == nullptr && length != 0U) || length > PayloadCapacity) {
            return RealtimePublicationResult::PayloadTooLarge;
        }
        for (size_t i = 0U; i < SlotCapacity; ++i) {
            if (entries_[i].inUse) {
                continue;
            }
            Entry& entry = entries_[i];
            entry.inUse = true;
            entry.metadata = metadata;
            entry.type = type;
            entry.length = length;
            if (length != 0U) {
                memcpy(entry.payload, payload, length);
            }
            index = i;
            return RealtimePublicationResult::Accepted;
        }
        return RealtimePublicationResult::Busy;
    }
    Entry* entry(size_t index) {
        return index < SlotCapacity && entries_[index].inUse ? &entries_[index] : nullptr;
    }
    void release(size_t index) {
        if (index < SlotCapacity) {
            entries_[index].inUse = false;
        }
    }

private:
    Entry entries_[SlotCapacity] {};
};

} // namespace Web
} // namespace AquaCore
