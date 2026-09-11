#include "services/AlarmService.h"

namespace gassense {

void AlarmService::update(const GasState&, bool serviceMode, bool soundEnabled) {
    if (serviceMode || !soundEnabled) {
        return;
    }

    // TODO: priorytety + stałe wzory buzzera.
}

void AlarmService::muteCurrentAlarm() {
    currentAlarmMuted_ = true;
}

void AlarmService::testBuzzer() {
    // TODO
}

}
