#pragma once
#include "../domain/Types.h"

namespace gassense {

class AlarmService {
public:
    void update(const GasState& state, bool serviceMode, bool soundEnabled);
    void muteCurrentAlarm();
    void testBuzzer();

private:
    bool currentAlarmMuted_ = false;
};

} // namespace gassense
