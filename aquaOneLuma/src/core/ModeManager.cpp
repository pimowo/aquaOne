#include "ModeManager.h"

namespace LumaSense {

void ModeManager::resetToNormal() {
    mode_ = OperatingMode::Normal;
    baseMode_ = OperatingMode::Normal;
    returnMode_ = OperatingMode::Normal;

    manualActive_ = false;
    channelTestActive_ = false;
    previewActive_ = false;
    simulationActive_ = false;
}

OperatingMode ModeManager::mode() const {
    return mode_;
}

OperatingMode ModeManager::baseMode() const {
    return baseMode_;
}

OperatingMode ModeManager::returnMode() const {
    return returnMode_;
}

bool ModeManager::enterService() {
    if (
        mode_ == OperatingMode::Off ||
        baseMode_ == OperatingMode::Service
    ) {
        return false;
    }

    baseMode_ = OperatingMode::Service;
    refreshModes();
    return true;
}

bool ModeManager::exitService() {
    if (
        mode_ == OperatingMode::Off ||
        baseMode_ != OperatingMode::Service
    ) {
        return false;
    }

    baseMode_ = OperatingMode::Normal;
    refreshModes();
    return true;
}

bool ModeManager::enterManual() {
    if (
        manualActive_ ||
        !canEnterOverride(OperatingMode::Manual)
    ) {
        return false;
    }

    manualActive_ = true;
    refreshModes();
    return true;
}

bool ModeManager::exitManual() {
    if (
        mode_ != OperatingMode::Manual ||
        !manualActive_
    ) {
        return false;
    }

    manualActive_ = false;
    refreshModes();
    return true;
}

bool ModeManager::enterChannelTest() {
    if (
        channelTestActive_ ||
        !canEnterOverride(OperatingMode::ChannelTest)
    ) {
        return false;
    }

    channelTestActive_ = true;
    refreshModes();
    return true;
}

bool ModeManager::exitChannelTest() {
    if (
        mode_ != OperatingMode::ChannelTest ||
        !channelTestActive_
    ) {
        return false;
    }

    channelTestActive_ = false;
    refreshModes();
    return true;
}

bool ModeManager::enterPreview() {
    if (
        previewActive_ ||
        !canEnterOverride(OperatingMode::Preview)
    ) {
        return false;
    }

    previewActive_ = true;
    refreshModes();
    return true;
}

bool ModeManager::exitPreview() {
    if (
        mode_ != OperatingMode::Preview ||
        !previewActive_
    ) {
        return false;
    }

    previewActive_ = false;
    refreshModes();
    return true;
}

bool ModeManager::enterSimulation() {
    if (
        simulationActive_ ||
        !canEnterOverride(OperatingMode::Simulation)
    ) {
        return false;
    }

    simulationActive_ = true;
    refreshModes();
    return true;
}

bool ModeManager::exitSimulation() {
    if (
        mode_ != OperatingMode::Simulation ||
        !simulationActive_
    ) {
        return false;
    }

    simulationActive_ = false;
    refreshModes();
    return true;
}

bool ModeManager::enterOff() {
    if (mode_ == OperatingMode::Off) {
        return false;
    }

    manualActive_ = false;
    channelTestActive_ = false;
    previewActive_ = false;
    simulationActive_ = false;

    baseMode_ = OperatingMode::Normal;
    mode_ = OperatingMode::Off;
    returnMode_ = OperatingMode::Normal;

    return true;
}

bool ModeManager::exitOff() {
    if (mode_ != OperatingMode::Off) {
        return false;
    }

    mode_ = OperatingMode::Normal;
    baseMode_ = OperatingMode::Normal;
    refreshModes();
    return true;
}

uint8_t ModeManager::priorityOf(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Off:
            return 7;

        case OperatingMode::ChannelTest:
            return 6;

        case OperatingMode::Manual:
            return 5;

        case OperatingMode::Preview:
            return 4;

        case OperatingMode::Simulation:
            return 3;

        case OperatingMode::Service:
            return 2;

        case OperatingMode::Normal:
        default:
            return 1;
    }
}

bool ModeManager::canEnterOverride(
    OperatingMode requestedMode
) const {
    return
        mode_ != OperatingMode::Off &&
        priorityOf(requestedMode) >
            priorityOf(mode_);
}

void ModeManager::refreshModes() {
    if (mode_ == OperatingMode::Off) {
        returnMode_ = OperatingMode::Normal;
        return;
    }

    if (channelTestActive_) {
        mode_ = OperatingMode::ChannelTest;
    } else if (manualActive_) {
        mode_ = OperatingMode::Manual;
    } else if (previewActive_) {
        mode_ = OperatingMode::Preview;
    } else if (simulationActive_) {
        mode_ = OperatingMode::Simulation;
    } else {
        mode_ = baseMode_;
    }

    switch (mode_) {
        case OperatingMode::ChannelTest:
            if (manualActive_) {
                returnMode_ = OperatingMode::Manual;
            } else if (previewActive_) {
                returnMode_ = OperatingMode::Preview;
            } else if (simulationActive_) {
                returnMode_ = OperatingMode::Simulation;
            } else {
                returnMode_ = baseMode_;
            }
            break;

        case OperatingMode::Manual:
            if (previewActive_) {
                returnMode_ = OperatingMode::Preview;
            } else if (simulationActive_) {
                returnMode_ = OperatingMode::Simulation;
            } else {
                returnMode_ = baseMode_;
            }
            break;

        case OperatingMode::Preview:
            returnMode_ =
                simulationActive_
                    ? OperatingMode::Simulation
                    : baseMode_;
            break;

        case OperatingMode::Simulation:
            returnMode_ = baseMode_;
            break;

        case OperatingMode::Service:
        case OperatingMode::Normal:
        default:
            returnMode_ = baseMode_;
            break;
    }
}

} // namespace LumaSense