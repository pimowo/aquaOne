#pragma once

namespace AquaCore {
namespace Alarms {

// The project owns AlarmId and supplies the semantic condition. This owner is
// volatile; it neither stores history nor publishes events or runtime state.
enum class AlarmTransitionResult {
    NoChange,
    Activated,
    Cleared,
    ConditionRecovered,
    Acknowledged,
    LatchCleared,
    Reoccurred,
    ConditionConfirmed
};

// The only cross-restart fact. The application owns storage and its mapping
// from a record to a Domain alarm; condition and ACK are runtime-local.
struct AlarmPersistentState {
    explicit AlarmPersistentState(bool pending = false)
        : latchedPending(pending) {}

    bool latchedPending;
};

enum class AlarmRestoreResult {
    Restored,
    AlreadyAttempted,
    LiveStarted,
    IncompatibleState
};

template <typename AlarmId>
struct AlarmSnapshot {
    AlarmId id;
    bool conditionActive;
    bool alarmActive;
    bool acknowledged;
    bool latched;
};

template <typename AlarmId>
class AlarmState {
public:
    AlarmState(AlarmId id, bool latched)
        : id_(id), latched_(latched), conditionActive_(false),
          alarmActive_(false), acknowledged_(false), conditionKnown_(false),
          restoreAttempted_(false), liveStarted_(false) {}

    AlarmState(const AlarmState&) = delete;
    AlarmState& operator=(const AlarmState&) = delete;

    AlarmSnapshot<AlarmId> snapshot() const {
        return {id_, conditionActive_, alarmActive_, acknowledged_, latched_};
    }

    AlarmPersistentState persistentState() const {
        return AlarmPersistentState(latched_ && alarmActive_);
    }

    // Initialization only. The first attempt consumes this boundary even on
    // failure; incompatible data never partially changes the live alarm.
    AlarmRestoreResult restore(AlarmPersistentState state) {
        if (restoreAttempted_) {
            return AlarmRestoreResult::AlreadyAttempted;
        }
        restoreAttempted_ = true;
        if (liveStarted_) {
            return AlarmRestoreResult::LiveStarted;
        }
        if (!latched_ && state.latchedPending) {
            return AlarmRestoreResult::IncompatibleState;
        }
        alarmActive_ = state.latchedPending;
        acknowledged_ = false;
        return AlarmRestoreResult::Restored;
    }

    AlarmTransitionResult setCondition(bool active) {
        liveStarted_ = true;
        const bool wasKnown = conditionKnown_;
        conditionKnown_ = true;
        if (active == conditionActive_) {
            return AlarmTransitionResult::NoChange;
        }

        conditionActive_ = active;
        if (active) {
            const bool reoccurred = alarmActive_;
            alarmActive_ = true;
            acknowledged_ = false;
            if (reoccurred && !wasKnown) {
                return AlarmTransitionResult::ConditionConfirmed;
            }
            return reoccurred ? AlarmTransitionResult::Reoccurred
                             : AlarmTransitionResult::Activated;
        }

        if (latched_) {
            return AlarmTransitionResult::ConditionRecovered;
        }

        alarmActive_ = false;
        acknowledged_ = false;
        return AlarmTransitionResult::Cleared;
    }

    AlarmTransitionResult acknowledge() {
        liveStarted_ = true;
        if (!alarmActive_ || acknowledged_) {
            return AlarmTransitionResult::NoChange;
        }
        acknowledged_ = true;
        return AlarmTransitionResult::Acknowledged;
    }

    AlarmTransitionResult clearLatched() {
        liveStarted_ = true;
        if (!latched_ || !conditionKnown_ || conditionActive_ || !alarmActive_) {
            return AlarmTransitionResult::NoChange;
        }
        alarmActive_ = false;
        acknowledged_ = false;
        return AlarmTransitionResult::LatchCleared;
    }

private:
    const AlarmId id_;
    const bool latched_;
    bool conditionActive_;
    bool alarmActive_;
    bool acknowledged_;
    bool conditionKnown_;
    bool restoreAttempted_;
    bool liveStarted_;
};

} // namespace Alarms
} // namespace AquaCore
