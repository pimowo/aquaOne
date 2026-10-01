#pragma once

#include <stdint.h>

#include "AquaCore/System/RuntimeIdentity.h"

namespace AquaCore {
namespace Events {

class EventMetadataSequencer;
class EventMetadataSequencerTestAccess;

// A position within one runtime event stream; zero was never issued.
class EventSequence {
public:
    EventSequence() : value_(0U) {}
    static bool fromValue(uint64_t value, EventSequence& output);
    bool isValid() const { return value_ != 0U; }
    uint64_t value() const { return value_; }
    bool operator==(const EventSequence& other) const { return value_ == other.value_; }
    bool operator!=(const EventSequence& other) const { return !(*this == other); }

private:
    explicit EventSequence(uint64_t value) : value_(value) {}
    uint64_t value_;
    friend class EventMetadataSequencer;
};

// Technical, public metadata only. Domain event types remain independent.
class EventMetadata {
public:
    EventMetadata() : runtimeIdentity_(), sequence_() {}
    static bool fromValues(
        Identity::RuntimeIdentity runtimeIdentity,
        EventSequence sequence,
        EventMetadata& output
    );
    bool isValid() const {
        return runtimeIdentity_.isValid() && sequence_.isValid();
    }
    Identity::RuntimeIdentity runtimeIdentity() const { return runtimeIdentity_; }
    EventSequence sequence() const { return sequence_; }

private:
    EventMetadata(Identity::RuntimeIdentity runtimeIdentity, EventSequence sequence)
        : runtimeIdentity_(runtimeIdentity), sequence_(sequence) {}

    Identity::RuntimeIdentity runtimeIdentity_;
    EventSequence sequence_;
    friend class EventMetadataSequencer;
};

enum class EventIssueResult : uint8_t {
    Success,
    InvalidRuntimeIdentity,
    Exhausted
};

// Application owns one sequencer per runtime event stream. Issue calls must be
// serialized in one owner context; concurrent/ISR issuance is not supported.
class EventMetadataSequencer {
public:
    explicit EventMetadataSequencer(Identity::RuntimeIdentity runtimeIdentity)
        : runtimeIdentity_(runtimeIdentity), lastIssued_(0U) {}
    EventMetadataSequencer(const EventMetadataSequencer&) = delete;
    EventMetadataSequencer& operator=(const EventMetadataSequencer&) = delete;

    EventIssueResult issue(EventMetadata& output);
    bool isExhausted() const { return lastIssued_ == UINT64_MAX; }

private:
    const Identity::RuntimeIdentity runtimeIdentity_;
    uint64_t lastIssued_;

    // Allows a test to reach the 64-bit boundary without issuing 2^64 events.
    friend class EventMetadataSequencerTestAccess;
};

} // namespace Events
} // namespace AquaCore
