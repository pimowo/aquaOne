#pragma once

#include "AquaCore/Alarms/AlarmState.h"

namespace AquaCore {
namespace Alarms {

// Composition Root owns both objects. The AlarmState must outlive this borrowed
// application capability; Domain retains the state owner for condition updates.
template <typename AlarmId>
class AlarmControl {
public:
    explicit AlarmControl(AlarmState<AlarmId>& alarm) : alarm_(alarm) {}

    AlarmControl(const AlarmControl&) = delete;
    AlarmControl& operator=(const AlarmControl&) = delete;

    AlarmTransitionResult acknowledge() {
        return alarm_.acknowledge();
    }

    AlarmTransitionResult clearLatched() {
        return alarm_.clearLatched();
    }

private:
    AlarmState<AlarmId>& alarm_;
};

} // namespace Alarms
} // namespace AquaCore
