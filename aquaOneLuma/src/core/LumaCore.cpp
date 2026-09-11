#include "LumaCore.h"

#include <cstring>

#include "Constants.h"
#include "../storage/ConfigValidator.h"

namespace LumaSense {
namespace {

void addFingerprintByte(
    uint32_t& fingerprint,
    uint8_t value
) {
    fingerprint ^= value;
    fingerprint *= 16777619UL;
}

void addFingerprintUint16(
    uint32_t& fingerprint,
    uint16_t value
) {
    addFingerprintByte(
        fingerprint,
        static_cast<uint8_t>(value)
    );

    addFingerprintByte(
        fingerprint,
        static_cast<uint8_t>(value >> 8)
    );
}

void addFingerprintFloat(
    uint32_t& fingerprint,
    float value
) {
    uint32_t bits = 0;

    static_assert(
        sizeof(bits) == sizeof(value),
        "Unexpected float size"
    );

    std::memcpy(
        &bits,
        &value,
        sizeof(bits)
    );

    for (uint8_t byte = 0; byte < sizeof(bits); ++byte) {
        addFingerprintByte(
            fingerprint,
            static_cast<uint8_t>(
                bits >> (byte * 8)
            )
        );
    }
}

constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;
constexpr int32_t TIME_JUMP_TOLERANCE_SECONDS = 3;

bool isSignificantTimeJump(
    uint32_t previousSecondOfDay,
    uint32_t currentSecondOfDay,
    uint32_t previousMs,
    uint32_t currentMs
) {
    const uint32_t elapsedMs =
        currentMs - previousMs;

    uint32_t expectedElapsedSeconds =
        elapsedMs / 1000UL;

    if (elapsedMs % 1000UL >= 500UL) {
        ++expectedElapsedSeconds;
    }

    expectedElapsedSeconds %=
        SECONDS_PER_DAY;

    const uint32_t actualElapsedSeconds =
        (
            currentSecondOfDay +
            SECONDS_PER_DAY -
            previousSecondOfDay
        ) % SECONDS_PER_DAY;

    int32_t difference =
        static_cast<int32_t>(actualElapsedSeconds) -
        static_cast<int32_t>(expectedElapsedSeconds);

    const int32_t halfDay =
        static_cast<int32_t>(SECONDS_PER_DAY / 2UL);

    if (difference > halfDay) {
        difference -=
            static_cast<int32_t>(SECONDS_PER_DAY);
    } else if (difference < -halfDay) {
        difference +=
            static_cast<int32_t>(SECONDS_PER_DAY);
    }

    return
        difference > TIME_JUMP_TOLERANCE_SECONDS ||
        difference < -TIME_JUMP_TOLERANCE_SECONDS;
}

bool channelLevelsDiffer(
    const ChannelLevels& first,
    const ChannelLevels& second
) {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        if (first.value[channel] != second.value[channel]) {
            return true;
        }
    }

    return false;
}

uint32_t profileOutputFingerprint(
    const Profile& profile
) {
    uint32_t fingerprint = 2166136261UL;

    addFingerprintUint16(
        fingerprint,
        profile.dayStartMinute
    );

    addFingerprintUint16(
        fingerprint,
        profile.dayEndMinute
    );

    addFingerprintByte(
        fingerprint,
        profile.nightEnabled ? 1U : 0U
    );

    for (uint8_t stage = 0; stage < DAY_STAGE_COUNT; ++stage) {
        for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
            addFingerprintFloat(
                fingerprint,
                profile
                    .stages[stage]
                    .levels
                    .value[channel]
            );
        }
    }

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        addFingerprintFloat(
            fingerprint,
            profile.nightLevels.value[channel]
        );
    }

    return fingerprint;
}

} // namespace

bool LumaCore::begin(
    DeviceConfig& config,
    uint32_t nowMs
) {
    modeManager_.resetToNormal();

    const bool configValid =
        ConfigValidator::validate(config);

    config_ =
        configValid ? &config : nullptr;

    state_ = {};

    state_.mode =
        modeManager_.mode();

    state_.returnMode =
        modeManager_.returnMode();

    state_.timeValid = false;

    // MANUAL
    manualLevels_ = {};
    lastManualLevels_ = {};

    manualStartedMs_ = 0;
    manualTimeoutMs_ = 0;
    manualTimerActive_ = false;

    // CHANNEL TEST
    channelTestLevels_ = {};
    lastChannelTestLevels_ = {};

    channelTestChannel_ = 0;

    channelTestStartedMs_ = 0;
    channelTestTimerActive_ = false;

    // PREVIEW
    previewProfileIndex_ = 0;
    previewStageIndex_ = 0;

    lastPreviewProfileIndex_ = 0;
    lastPreviewStageIndex_ = 0;

    // SIMULATION
    simulationStartedMs_ = 0;
    simulationDurationMs_ = 0;

    simulationActive_ = false;
    simulationEntryActive_ = false;
    simulationCompletionPending_ = false;

    for (
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        state_.requestedLevels.value[ch] =
            0.0f;

        state_.actualLevels.value[ch] =
            0.0f;
    }

    state_.transitionActive = false;

    transitionEngine_.cancel();

    initialized_ = false;

    lastRealSecondOfDay_ = 0;
    lastTimeObservationMs_ = 0;
    timeObservationValid_ = false;

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        profileFingerprints_[profile] =
            profileOutputFingerprint(config.profiles[profile]);

        profileFingerprintInitialized_[profile] =
            configValid;
    }

    lastProfileIndex_ = 0;

    lastDayState_ =
        DayState::Day;

    lastMode_ =
        state_.mode;

    (void)nowMs;

    return configValid;
}

// =========================================================
// SERVICE
// =========================================================

void LumaCore::enterService() {
    if (modeManager_.enterService()) {
        syncModeState();
    }
}

void LumaCore::exitService() {
    if (modeManager_.exitService()) {
        syncModeState();
    }
}

// =========================================================
// MANUAL
// =========================================================

void LumaCore::enterManual(
    const ChannelLevels& levels,
    uint16_t timeoutMinutes,
    uint32_t nowMs
) {
    // Accepted values: 0 / 15 / 30 / 60 minutes.
    if (
        timeoutMinutes != 15 &&
        timeoutMinutes != 30 &&
        timeoutMinutes != 60
    ) {
        timeoutMinutes = 0;
    }

    if (!modeManager_.enterManual()) {
        return;
    }

    manualLevels_ = levels;
    lastManualLevels_ = levels;

    manualStartedMs_ =
        nowMs;

    if (timeoutMinutes == 0) {
        manualTimeoutMs_ = 0;
        manualTimerActive_ = false;
    } else {
        manualTimeoutMs_ =
            static_cast<uint32_t>(
                timeoutMinutes
            ) * 60UL * 1000UL;

        manualTimerActive_ = true;
    }

    syncModeState();
}

void LumaCore::setManualLevels(
    const ChannelLevels& levels
) {
    manualLevels_ = levels;
}

void LumaCore::exitManual() {
    if (!modeManager_.exitManual()) {
        return;
    }

    manualTimerActive_ = false;
    manualTimeoutMs_ = 0;

    syncModeState();
}

// =========================================================
// CHANNEL TEST
// =========================================================

void LumaCore::enterChannelTest(
    uint8_t channel,
    float percent
) {
    if (channel >= CHANNEL_COUNT) {
        return;
    }

    if (percent < 0.0f) {
        percent = 0.0f;
    }

    if (percent > 100.0f) {
        percent = 100.0f;
    }

    if (!modeManager_.enterChannelTest()) {
        return;
    }

    channelTestChannel_ =
        channel;

    channelTestLevels_ = {};

    channelTestLevels_
        .value[channelTestChannel_] =
            percent;

    lastChannelTestLevels_ =
        channelTestLevels_;

    channelTestTimerActive_ = false;

    syncModeState();
}

void LumaCore::setChannelTestLevel(
    float percent
) {
    if (
        modeManager_.mode() !=
        OperatingMode::ChannelTest
    ) {
        return;
    }

    if (percent < 0.0f) {
        percent = 0.0f;
    }

    if (percent > 100.0f) {
        percent = 100.0f;
    }

    channelTestLevels_ = {};

    channelTestLevels_
        .value[channelTestChannel_] =
            percent;
}

void LumaCore::exitChannelTest() {
    if (!modeManager_.exitChannelTest()) {
        return;
    }

    channelTestTimerActive_ = false;

    syncModeState();
}

// =========================================================
// PREVIEW
// =========================================================

void LumaCore::enterPreview(
    uint8_t profileIndex,
    uint8_t stageIndex
) {
    if (
        profileIndex >= PROFILE_COUNT ||
        stageIndex >= DAY_STAGE_COUNT
    ) {
        return;
    }

    if (!modeManager_.enterPreview()) {
        return;
    }

    previewProfileIndex_ =
        profileIndex;

    previewStageIndex_ =
        stageIndex;

    lastPreviewProfileIndex_ =
        profileIndex;

    lastPreviewStageIndex_ =
        stageIndex;

    syncModeState();
}

void LumaCore::setPreviewProfile(
    uint8_t profileIndex
) {
    if (
        modeManager_.mode() !=
            OperatingMode::Preview ||
        profileIndex >= PROFILE_COUNT
    ) {
        return;
    }

    previewProfileIndex_ =
        profileIndex;
}

void LumaCore::setPreviewStage(
    uint8_t stageIndex
) {
    if (
        modeManager_.mode() !=
        OperatingMode::Preview
    ) {
        return;
    }

    if (stageIndex >= DAY_STAGE_COUNT) {
        return;
    }

    previewStageIndex_ =
        stageIndex;
}

void LumaCore::exitPreview() {
    if (modeManager_.exitPreview()) {
        syncModeState();
    }
}

// =========================================================
// SIMULATION
// =========================================================

void LumaCore::enterSimulation(
    uint8_t durationMinutes,
    uint32_t nowMs
) {
    if (durationMinutes < 1) {
        durationMinutes = 1;
    }

    if (durationMinutes > 15) {
        durationMinutes = 15;
    }

    if (!modeManager_.enterSimulation()) {
        return;
    }

    simulationStartedMs_ = 0;

    simulationDurationMs_ =
        static_cast<uint32_t>(
            durationMinutes
        ) * 60UL * 1000UL;

    simulationActive_ = false;
    simulationEntryActive_ = true;
    simulationCompletionPending_ = false;

    syncModeState();

    (void)nowMs;
}

void LumaCore::exitSimulation() {
    if (!modeManager_.exitSimulation()) {
        return;
    }

    simulationActive_ = false;
    simulationEntryActive_ = false;
    simulationCompletionPending_ = false;

    syncModeState();
}

// =========================================================
// OFF
// =========================================================

void LumaCore::enterOff() {
    if (!modeManager_.enterOff()) {
        return;
    }

    manualTimerActive_ = false;
    manualTimeoutMs_ = 0;
    channelTestTimerActive_ = false;
    simulationActive_ = false;
    simulationEntryActive_ = false;
    simulationCompletionPending_ = false;

    syncModeState();
}

void LumaCore::exitOff() {
    if (!modeManager_.exitOff()) {
        return;
    }

    initialized_ = false;

    syncModeState();
}

// =========================================================
// UPDATE
// =========================================================

void LumaCore::update(
    const LocalTime& localTime,
    uint32_t nowMs
) {
    if (config_ == nullptr) {
        return;
    }

    state_.mode =
        modeManager_.mode();

    state_.returnMode =
        modeManager_.returnMode();

    bool profileDataChanged[PROFILE_COUNT] {};

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
        const uint32_t fingerprint =
            profileOutputFingerprint(config_->profiles[profile]);

        profileDataChanged[profile] =
            profileFingerprintInitialized_[profile] &&
            fingerprint != profileFingerprints_[profile];

        profileFingerprints_[profile] =
            fingerprint;

        profileFingerprintInitialized_[profile] =
            true;
    }

    // =====================================================
    // CHANNEL TEST timeout
    // =====================================================

    if (
        state_.mode ==
        OperatingMode::ChannelTest
    ) {
        if (!channelTestTimerActive_) {
            channelTestStartedMs_ =
                nowMs;

            channelTestTimerActive_ =
                true;
        }

        else if (
            nowMs -
            channelTestStartedMs_ >=
            CHANNEL_TEST_TIMEOUT_MS
        ) {
            if (modeManager_.exitChannelTest()) {
                channelTestTimerActive_ =
                    false;

                syncModeState();
            }
        }
    } else {
        channelTestTimerActive_ =
            false;
    }

    // =====================================================
    // MANUAL timeout
    // =====================================================

    if (
        state_.mode ==
            OperatingMode::Manual &&
        manualTimerActive_ &&
        manualTimeoutMs_ > 0
    ) {
        if (
            nowMs -
            manualStartedMs_ >=
            manualTimeoutMs_
        ) {
            if (modeManager_.exitManual()) {
                manualTimerActive_ = false;
                manualTimeoutMs_ = 0;
                syncModeState();
            }
        }
    }

    // =====================================================
    // SIMULATION auto exit after the final frame
    // =====================================================

    if (
        state_.mode ==
            OperatingMode::Simulation &&
        simulationCompletionPending_
    ) {
        if (modeManager_.exitSimulation()) {
            simulationActive_ = false;
            simulationEntryActive_ = false;
            simulationCompletionPending_ = false;
            syncModeState();
        }
    }

    // =====================================================
    // OFF
    // =====================================================

    if (
        state_.mode ==
        OperatingMode::Off
    ) {
        transitionEngine_.cancel();

        for (
            uint8_t ch = 0;
            ch < CHANNEL_COUNT;
            ++ch
        ) {
            state_.requestedLevels.value[ch] =
                0.0f;

            state_.actualLevels.value[ch] =
                0.0f;
        }

        state_.transitionActive = false;

        initialized_ = false;
        timeObservationValid_ = false;

        lastMode_ =
            OperatingMode::Off;

        return;
    }

    const ChannelLevels previousRequestedLevels =
        state_.requestedLevels;

    // =====================================================
    // Czas
    // =====================================================

    state_.timeValid =
        localTime.valid;

    if (!localTime.valid) {
        transitionEngine_.cancel();

        state_.currentMinuteOfDay = 0;

        for (
            uint8_t ch = 0;
            ch < CHANNEL_COUNT;
            ++ch
        ) {
            state_.requestedLevels.value[ch] =
                0.0f;

            state_.actualLevels.value[ch] =
                0.0f;
        }

        state_.transitionActive = false;

        initialized_ = false;
        timeObservationValid_ = false;

        lastMode_ =
            state_.mode;

        return;
    }

    state_.currentMinuteOfDay =
        localTime.minuteOfDay;

    const uint32_t realSecondOfDay =
        static_cast<uint32_t>(
            localTime.hour
        ) * 3600UL +
        static_cast<uint32_t>(
            localTime.minute
        ) * 60UL +
        static_cast<uint32_t>(
            localTime.second
        );

    const bool scheduleMode =
        state_.mode == OperatingMode::Normal ||
        state_.mode == OperatingMode::Service;

    bool timeJumpDetected = false;

    if (scheduleMode) {
        if (timeObservationValid_) {
            timeJumpDetected =
                isSignificantTimeJump(
                    lastRealSecondOfDay_,
                    realSecondOfDay,
                    lastTimeObservationMs_,
                    nowMs
                );
        }

        lastRealSecondOfDay_ =
            realSecondOfDay;

        lastTimeObservationMs_ =
            nowMs;

        timeObservationValid_ =
            true;
    } else {
        timeObservationValid_ =
            false;
    }

    // =====================================================
    // Profil bazowy
    // =====================================================

    uint8_t profileIndex;

    if (
        state_.mode ==
        OperatingMode::Service
    ) {
        profileIndex =
            config_->serviceProfileIndex;
    } else {
        profileIndex =
            config_->activeProfileIndex;
    }

    if (
        profileIndex >= PROFILE_COUNT
    ) {
        profileIndex = 0;
    }

    // =====================================================
    // REQUESTED
    // =====================================================

    if (
        state_.mode ==
        OperatingMode::Simulation
    ) {
        const Profile& profile =
            config_->profiles[
                profileIndex
            ];

        const uint32_t startSecond =
            static_cast<uint32_t>(
                profile.dayStartMinute
            ) * 60UL;

        uint32_t simulatedSecondOfDay =
            startSecond;

        bool finalFrame = false;

        if (
            simulationActive_ &&
            simulationDurationMs_ > 0
        ) {
            const uint32_t elapsedMs =
                nowMs -
                simulationStartedMs_;

            float progress =
                static_cast<float>(
                    elapsedMs
                ) /
                static_cast<float>(
                    simulationDurationMs_
                );

            if (progress < 0.0f) {
                progress = 0.0f;
            }

            if (progress >= 1.0f) {
                progress = 1.0f;
                finalFrame = true;
            }

            const uint32_t endSecond =
                static_cast<uint32_t>(
                    profile.dayEndMinute
                ) * 60UL;

            const uint32_t photoperiodSeconds =
                endSecond > startSecond
                    ? endSecond - startSecond
                    : 0;

            if (!finalFrame) {
                uint32_t offsetSeconds =
                    static_cast<uint32_t>(
                        progress *
                        static_cast<float>(
                            photoperiodSeconds
                        )
                    );

                if (
                    photoperiodSeconds > 0 &&
                    offsetSeconds >= photoperiodSeconds
                ) {
                    offsetSeconds =
                        photoperiodSeconds - 1;
                }

                simulatedSecondOfDay =
                    startSecond +
                    offsetSeconds;
            }
        }

        if (finalFrame) {
            state_.dayState =
                DayState::Day;

            state_.currentStageIndex =
                DAY_STAGE_COUNT - 1;

            state_.nextStageIndex =
                DAY_STAGE_COUNT - 1;

            state_.requestedLevels =
                profile
                    .stages[DAY_STAGE_COUNT - 1]
                    .levels;
        } else {
            const DayCalculation calculation =
                dayEngine_.calculateSeconds(
                    profile,
                    simulatedSecondOfDay
                );

            state_.dayState =
                calculation.dayState;

            state_.currentStageIndex =
                calculation.currentStageIndex;

            state_.nextStageIndex =
                calculation.nextStageIndex;

            state_.requestedLevels =
                calculation.levels;
        }
    }

    else if (
        state_.mode ==
        OperatingMode::Preview
    ) {
        state_.requestedLevels =
            config_
                ->profiles[previewProfileIndex_]
                .stages[previewStageIndex_]
                .levels;
    }

    else if (
        state_.mode ==
        OperatingMode::ChannelTest
    ) {
        state_.requestedLevels =
            channelTestLevels_;
    }

    else if (
        state_.mode ==
        OperatingMode::Manual
    ) {
        state_.requestedLevels =
            manualLevels_;
    }

    else {
        const Profile& profile =
            config_->profiles[
                profileIndex
            ];

        const DayCalculation calculation =
            dayEngine_.calculateSeconds(
                profile,
                realSecondOfDay
            );

        state_.dayState =
            calculation.dayState;

        state_.currentStageIndex =
            calculation.currentStageIndex;

        state_.nextStageIndex =
            calculation.nextStageIndex;

        state_.requestedLevels =
            calculation.levels;
    }

    // =====================================================
    // Pierwszy start
    // =====================================================

    if (!initialized_) {
        ChannelLevels zero {};

        transitionEngine_.start(
            zero,
            state_.requestedLevels,
            STANDARD_TRANSITION_MS,
            nowMs
        );

        initialized_ = true;

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;

        if (
            state_.mode ==
            OperatingMode::Manual
        ) {
            lastManualLevels_ =
                manualLevels_;
        }

        if (
            state_.mode ==
            OperatingMode::ChannelTest
        ) {
            lastChannelTestLevels_ =
                channelTestLevels_;
        }

        if (
            state_.mode ==
            OperatingMode::Preview
        ) {
            lastPreviewProfileIndex_ =
                previewProfileIndex_;

            lastPreviewStageIndex_ =
                previewStageIndex_;
        }
    }

    // =====================================================
    // SIMULATION enter / exit
    // =====================================================

    else if (
        state_.mode != lastMode_ &&
        (
            state_.mode ==
                OperatingMode::Simulation ||
            lastMode_ ==
                OperatingMode::Simulation
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            SIMULATION_ENTRY_MS,
            nowMs
        );

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // PREVIEW enter / exit
    // =====================================================

    else if (
        state_.mode != lastMode_ &&
        (
            state_.mode ==
                OperatingMode::Preview ||
            lastMode_ ==
                OperatingMode::Preview
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            PREVIEW_SMOOTH_MS,
            nowMs
        );

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;

        if (
            state_.mode ==
            OperatingMode::Preview
        ) {
            lastPreviewProfileIndex_ =
                previewProfileIndex_;

            lastPreviewStageIndex_ =
                previewStageIndex_;
        }
    }

    // =====================================================
    // PREVIEW selection / data change
    // =====================================================

    else if (
        state_.mode ==
        OperatingMode::Preview
    ) {
        if (
            previewProfileIndex_ !=
                lastPreviewProfileIndex_ ||
            previewStageIndex_ !=
                lastPreviewStageIndex_ ||
            (
                profileDataChanged[previewProfileIndex_] &&
                channelLevelsDiffer(
                    state_.requestedLevels,
                    previousRequestedLevels
                )
            )
        ) {
            const ChannelLevels from =
                transitionEngine_.isActive()
                    ? transitionEngine_.update(
                        nowMs
                    )
                    : previousRequestedLevels;

            transitionEngine_.start(
                from,
                state_.requestedLevels,
                PREVIEW_SMOOTH_MS,
                nowMs
            );

            lastPreviewProfileIndex_ =
                previewProfileIndex_;

            lastPreviewStageIndex_ =
                previewStageIndex_;
        }

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // CHANNEL TEST enter / exit
    // =====================================================

    else if (
        state_.mode != lastMode_ &&
        (
            state_.mode ==
                OperatingMode::ChannelTest ||
            lastMode_ ==
                OperatingMode::ChannelTest
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            PREVIEW_SMOOTH_MS,
            nowMs
        );

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;

        if (
            state_.mode ==
            OperatingMode::ChannelTest
        ) {
            lastChannelTestLevels_ =
                channelTestLevels_;
        }
    }

    // =====================================================
    // CHANNEL TEST value change
    // =====================================================

    else if (
        state_.mode ==
        OperatingMode::ChannelTest
    ) {
        bool changed = false;

        for (
            uint8_t ch = 0;
            ch < CHANNEL_COUNT;
            ++ch
        ) {
            if (
                channelTestLevels_.value[ch] !=
                lastChannelTestLevels_.value[ch]
            ) {
                changed = true;
                break;
            }
        }

        if (changed) {
            const ChannelLevels from =
                transitionEngine_.isActive()
                    ? transitionEngine_.update(
                        nowMs
                    )
                    : previousRequestedLevels;

            transitionEngine_.start(
                from,
                channelTestLevels_,
                PREVIEW_SMOOTH_MS,
                nowMs
            );

            lastChannelTestLevels_ =
                channelTestLevels_;
        }

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // MANUAL enter / exit
    // =====================================================

    else if (
        state_.mode != lastMode_ &&
        (
            state_.mode ==
                OperatingMode::Manual ||
            lastMode_ ==
                OperatingMode::Manual
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            STANDARD_TRANSITION_MS,
            nowMs
        );

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;

        if (
            state_.mode ==
            OperatingMode::Manual
        ) {
            lastManualLevels_ =
                manualLevels_;
        }
    }

    // =====================================================
    // MANUAL value change
    // =====================================================

    else if (
        state_.mode ==
        OperatingMode::Manual
    ) {
        bool manualChanged = false;

        for (
            uint8_t ch = 0;
            ch < CHANNEL_COUNT;
            ++ch
        ) {
            if (
                manualLevels_.value[ch] !=
                lastManualLevels_.value[ch]
            ) {
                manualChanged = true;
                break;
            }
        }

        if (manualChanged) {
            const ChannelLevels from =
                transitionEngine_.isActive()
                    ? transitionEngine_.update(
                        nowMs
                    )
                    : previousRequestedLevels;

            transitionEngine_.start(
                from,
                manualLevels_,
                PREVIEW_SMOOTH_MS,
                nowMs
            );

            lastManualLevels_ =
                manualLevels_;
        }

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // Zmiana profilu
    // =====================================================

    else if (
        profileIndex !=
        lastProfileIndex_
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            STANDARD_TRANSITION_MS,
            nowMs
        );

        lastProfileIndex_ =
            profileIndex;

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // Zmiana danych aktywnego profilu
    // =====================================================

    else if (
        (
            state_.mode == OperatingMode::Normal ||
            state_.mode == OperatingMode::Service
        ) &&
        profileDataChanged[profileIndex] &&
        channelLevelsDiffer(
            state_.requestedLevels,
            previousRequestedLevels
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            STANDARD_TRANSITION_MS,
            nowMs
        );

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;
    }
    // =====================================================
    // DAY <-> NIGHT
    //
    // Ta gałąź celowo ma pierwszeństwo przed wykrytym
    // skokiem czasu, więc zachowuje przejście 5 s.
    // =====================================================

    else if (
        state_.dayState !=
        lastDayState_
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            NIGHT_TRANSITION_MS,
            nowMs
        );

        lastDayState_ =
            state_.dayState;

        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // Nagły skok czasu w NORMAL / SERVICE
    // =====================================================

    else if (
        timeJumpDetected &&
        channelLevelsDiffer(
            state_.requestedLevels,
            previousRequestedLevels
        )
    ) {
        const ChannelLevels from =
            transitionEngine_.isActive()
                ? transitionEngine_.update(
                    nowMs
                )
                : previousRequestedLevels;

        transitionEngine_.start(
            from,
            state_.requestedLevels,
            STANDARD_TRANSITION_MS,
            nowMs
        );

        lastMode_ =
            state_.mode;
    }

    else {
        lastMode_ =
            state_.mode;
    }

    // =====================================================
    // LightEngine
    // =====================================================

    ChannelLevels source {};

    if (
        transitionEngine_.isActive()
    ) {
        source =
            transitionEngine_.update(
                nowMs,
                state_.requestedLevels
            );
    } else {
        source =
            state_.requestedLevels;
    }

    state_.actualLevels =
        lightEngine_.process(
            source,
            *config_
        );

    state_.transitionActive =
        transitionEngine_.isActive();

    if (
        state_.mode ==
            OperatingMode::Simulation &&
        simulationEntryActive_ &&
        !state_.transitionActive
    ) {
        simulationStartedMs_ =
            nowMs;

        simulationEntryActive_ = false;
        simulationActive_ = true;
    }

    if (
        state_.mode ==
            OperatingMode::Simulation &&
        simulationActive_ &&
        simulationDurationMs_ > 0 &&
        nowMs - simulationStartedMs_ >=
            simulationDurationMs_ &&
        !state_.transitionActive
    ) {
        simulationActive_ = false;
        simulationCompletionPending_ = true;
    }
}

void LumaCore::syncModeState() {
    state_.mode =
        modeManager_.mode();

    state_.returnMode =
        modeManager_.returnMode();

    if (
        state_.mode != OperatingMode::Normal &&
        state_.mode != OperatingMode::Service
    ) {
        timeObservationValid_ = false;
    }
}

// =========================================================
// RuntimeState
// =========================================================

const RuntimeState& LumaCore::state() const {
    return state_;
}

} // namespace LumaSense