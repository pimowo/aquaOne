#pragma once

#include <stdint.h>

#include "AquaCore/Alarms/AlarmState.h"
#include "AquaCore/Events/EventSink.h"

namespace AquaCore {
namespace Alarms {

enum class AlarmEventKind : uint8_t {
    Activated,
    Acknowledged,
    ConditionRecovered,
    Cleared,
    Reoccurred
};

// Semantic notification with the state immediately after the transition.
// snapshot.id is the project-owned typed alarm identity. Current state must
// always be read from AlarmState; events are not an authoritative history.
template <typename AlarmId>
struct AlarmEvent {
    AlarmEventKind kind;
    AlarmSnapshot<AlarmId> snapshot;
};

template <typename AlarmId>
class AlarmEventEmitter {
public:
    // The sink is borrowed and must outlive this emitter. Emission is a
    // synchronous best-effort handoff with no rollback of the alarm state.
    explicit AlarmEventEmitter(Events::EventSink<AlarmEvent<AlarmId>>& sink)
        : sink_(sink) {}

    AlarmEventEmitter(const AlarmEventEmitter&) = delete;
    AlarmEventEmitter& operator=(const AlarmEventEmitter&) = delete;

    void emitTransition(AlarmTransitionResult transition,
                        const AlarmSnapshot<AlarmId>& after) const {
        AlarmEventKind kind;
        switch (transition) {
            case AlarmTransitionResult::Activated:
                kind = AlarmEventKind::Activated;
                break;
            case AlarmTransitionResult::Acknowledged:
                kind = AlarmEventKind::Acknowledged;
                break;
            case AlarmTransitionResult::ConditionRecovered:
                kind = AlarmEventKind::ConditionRecovered;
                break;
            case AlarmTransitionResult::Cleared:
            case AlarmTransitionResult::LatchCleared:
                kind = AlarmEventKind::Cleared;
                break;
            case AlarmTransitionResult::Reoccurred:
                kind = AlarmEventKind::Reoccurred;
                break;
            case AlarmTransitionResult::NoChange:
            case AlarmTransitionResult::ConditionConfirmed:
            default:
                // A restored latch is visible in the current snapshot, not
                // reported as a new occurrence during boot evaluation.
                return;
        }
        const AlarmEvent<AlarmId> event = {kind, after};
        sink_.emit(event);
    }

private:
    Events::EventSink<AlarmEvent<AlarmId>>& sink_;
};

} // namespace Alarms
} // namespace AquaCore
