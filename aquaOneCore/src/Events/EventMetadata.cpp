#include "AquaCore/Events/EventMetadata.h"

namespace AquaCore {
namespace Events {

bool EventSequence::fromValue(uint64_t value, EventSequence& output) {
    output = EventSequence(value);
    return output.isValid();
}

bool EventMetadata::fromValues(
    Identity::RuntimeIdentity runtimeIdentity,
    EventSequence sequence,
    EventMetadata& output
) {
    output = EventMetadata();
    if (!runtimeIdentity.isValid() || !sequence.isValid()) {
        return false;
    }
    output = EventMetadata(runtimeIdentity, sequence);
    return true;
}

EventIssueResult EventMetadataSequencer::issue(EventMetadata& output) {
    output = EventMetadata();
    if (!runtimeIdentity_.isValid()) {
        return EventIssueResult::InvalidRuntimeIdentity;
    }
    if (isExhausted()) {
        return EventIssueResult::Exhausted;
    }

    const uint64_t nextValue = lastIssued_ + 1U;
    output = EventMetadata(runtimeIdentity_, EventSequence(nextValue));
    lastIssued_ = nextValue;
    return EventIssueResult::Success;
}

} // namespace Events
} // namespace AquaCore
