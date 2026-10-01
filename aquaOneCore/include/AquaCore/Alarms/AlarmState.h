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
    Reoccurred
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
          alarmActive_(false), acknowledged_(false) {}

    AlarmState(const AlarmState&) = delete;
    AlarmState& operator=(const AlarmState&) = delete;

    AlarmSnapshot<AlarmId> snapshot() const {
        return {id_, conditionActive_, alarmActive_, acknowledged_, latched_};
    }

    AlarmTransitionResult setCondition(bool active) {
        if (active == conditionActive_) {
            return AlarmTransitionResult::NoChange;
        }

        conditionActive_ = active;
        if (active) {
            const bool reoccurred = alarmActive_;
            alarmActive_ = true;
            acknowledged_ = false;
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
        if (!alarmActive_ || acknowledged_) {
            return AlarmTransitionResult::NoChange;
        }
        acknowledged_ = true;
        return AlarmTransitionResult::Acknowledged;
    }

    AlarmTransitionResult clearLatched() {
        if (!latched_ || conditionActive_ || !alarmActive_) {
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
};

} // namespace Alarms
} // namespace AquaCore
