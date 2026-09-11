#pragma once

#include "RuntimeTypes.h"

namespace LumaSense {

class ModeManager {
public:
    void resetToNormal();

    OperatingMode mode() const;
    OperatingMode baseMode() const;
    OperatingMode returnMode() const;

    bool enterService();
    bool exitService();

    bool enterManual();
    bool exitManual();

    bool enterChannelTest();
    bool exitChannelTest();

    bool enterPreview();
    bool exitPreview();

    bool enterSimulation();
    bool exitSimulation();

    bool enterOff();
    bool exitOff();

private:
    OperatingMode mode_ = OperatingMode::Normal;
    OperatingMode baseMode_ = OperatingMode::Normal;
    OperatingMode returnMode_ = OperatingMode::Normal;

    bool manualActive_ = false;
    bool channelTestActive_ = false;
    bool previewActive_ = false;
    bool simulationActive_ = false;

    static uint8_t priorityOf(OperatingMode mode);

    bool canEnterOverride(OperatingMode mode) const;
    void refreshModes();
};

} // namespace LumaSense