#pragma once

#include "ModeManager.h"
#include "TransitionEngine.h"
#include "LightEngine.h"

#include "../profiles/DayEngine.h"
#include "../storage/ConfigTypes.h"
#include "../time/TimeTypes.h"

namespace LumaSense {

class LumaCore {
public:
    bool begin(
        DeviceConfig& config,
        uint32_t nowMs = 0
    );

    void update(
        const LocalTime& localTime,
        uint32_t nowMs
    );

    const RuntimeState& state() const;

    // =====================================================
    // SERVICE
    // =====================================================

    void enterService();
    void exitService();

    // =====================================================
    // MANUAL
    //
    // timeoutMinutes:
    // 0  = bez limitu
    // 15 = 15 minut
    // 30 = 30 minut
    // 60 = 60 minut
    // =====================================================

    void enterManual(
        const ChannelLevels& levels,
        uint16_t timeoutMinutes,
        uint32_t nowMs
    );

    void setManualLevels(
        const ChannelLevels& levels
    );

    void exitManual();

    // =====================================================
    // CHANNEL TEST
    // =====================================================

    void enterChannelTest(
        uint8_t channel,
        float percent
    );

    void setChannelTestLevel(
        float percent
    );

    void exitChannelTest();

    // =====================================================
    // PREVIEW
    // =====================================================

    void enterPreview(
        uint8_t profileIndex,
        uint8_t stageIndex
    );

    void setPreviewProfile(
        uint8_t profileIndex
    );

    void setPreviewStage(
        uint8_t stageIndex
    );

    void exitPreview();

    // =====================================================
    // SIMULATION
    // =====================================================

    void enterSimulation(
        uint8_t durationMinutes,
        uint32_t nowMs
    );

    void exitSimulation();

    // =====================================================
    // OFF
    // =====================================================

    void enterOff();
    void exitOff();

private:
    DeviceConfig* config_ = nullptr;

    RuntimeState state_ {};

    ModeManager modeManager_;
    TransitionEngine transitionEngine_;
    LightEngine lightEngine_;
    DayEngine dayEngine_;

    // =====================================================
    // MANUAL
    // =====================================================

    ChannelLevels manualLevels_ {};
    ChannelLevels lastManualLevels_ {};

    uint32_t manualStartedMs_ = 0;
    uint32_t manualTimeoutMs_ = 0;
    bool manualTimerActive_ = false;

    // =====================================================
    // CHANNEL TEST
    // =====================================================

    ChannelLevels channelTestLevels_ {};
    ChannelLevels lastChannelTestLevels_ {};

    uint8_t channelTestChannel_ = 0;

    uint32_t channelTestStartedMs_ = 0;
    bool channelTestTimerActive_ = false;

    // =====================================================
    // PREVIEW
    // =====================================================

    uint8_t previewProfileIndex_ = 0;
    uint8_t previewStageIndex_ = 0;

    uint8_t lastPreviewProfileIndex_ = 0;
    uint8_t lastPreviewStageIndex_ = 0;

    // =====================================================
    // SIMULATION
    // =====================================================

    uint32_t simulationStartedMs_ = 0;
    uint32_t simulationDurationMs_ = 0;

    bool simulationActive_ = false;
    bool simulationEntryActive_ = false;
    bool simulationCompletionPending_ = false;

    // =====================================================
    // Runtime
    // =====================================================

    bool initialized_ = false;

    uint32_t lastRealSecondOfDay_ = 0;
    uint32_t lastTimeObservationMs_ = 0;
    bool timeObservationValid_ = false;

    void syncModeState();

    uint32_t profileFingerprints_[PROFILE_COUNT] {};
    bool profileFingerprintInitialized_[PROFILE_COUNT] {};

    uint8_t lastProfileIndex_ = 0;

    DayState lastDayState_ =
        DayState::Day;

    OperatingMode lastMode_ =
        OperatingMode::Normal;
};

} // namespace LumaSense