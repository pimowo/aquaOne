#include "FirmwareApp.h"

#include <cmath>

#include "../../include/Constants.h"
#include "../storage/ConfigDefaults.h"
#include "../storage/ConfigValidator.h"

namespace LumaSense {
namespace {

bool validManualLevels(const ChannelLevels& levels) {
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (
            !std::isfinite(levels.value[channel]) ||
            levels.value[channel] < LEVEL_MIN_PERCENT ||
            levels.value[channel] > LEVEL_MAX_PERCENT
        ) {
            return false;
        }
    }
    return true;
}

bool validManualTimeout(uint16_t timeoutMinutes) {
    return
        timeoutMinutes == 0U ||
        timeoutMinutes == 15U ||
        timeoutMinutes == 30U ||
        timeoutMinutes == 60U;
}

bool reflectsMode(
    const RuntimeState& state,
    OperatingMode requested
) {
    if (requested == OperatingMode::Off) {
        return state.mode == OperatingMode::Off;
    }

    return
        state.mode == requested ||
        state.returnMode == requested;
}

} // namespace

FirmwareApp::FirmwareApp(
    HardwareInterface& hardware,
    StorageService& storage,
    TimeService& timeService
) :
    hardware_(hardware),
    storage_(storage),
    timeService_(timeService) {
}

bool FirmwareApp::begin(uint32_t nowMs) {
    status_ = {};
    cachedLocalTime_ = {};
    lastRtcReadMs_ = nowMs;
    rtcReadScheduled_ = false;

    // Hardware polarity is part of DeviceConfig and is copied by
    // hardware.begin(). Resolve NVS/defaults before touching PWM so
    // the first physical OFF level already uses the correct polarity.
    config_ = createDefaultConfig();

    status_.storageOk = storage_.begin();

    bool loadedFromNvs = false;

    if (status_.storageOk) {
        loadedFromNvs = storage_.load(config_);
    }

    if (
        loadedFromNvs &&
        ConfigValidator::validate(config_)
    ) {
        status_.configSource = ConfigSource::Nvs;
    } else {
        config_ = createDefaultConfig();
        status_.configSource = ConfigSource::Defaults;
    }

    status_.configValid =
        ConfigValidator::validate(config_);

    if (!status_.configValid) {
        return false;
    }

    status_.hardwareOk =
        hardware_.begin(config_.channels);

    if (!status_.hardwareOk) {
        enterHardwareFailSafe();
        return false;
    }

    (void)timeService_.begin();

    status_.rtcOk =
        timeService_.rtcInitialized();

    cachedLocalTime_ =
        timeService_.current();

    status_.timeValid =
        cachedLocalTime_.valid;

    rtcReadScheduled_ = true;

    status_.coreOk =
        core_.begin(config_, nowMs);

    if (!status_.coreOk) {
        enterHardwareFailSafe();
        return false;
    }

    status_.running = true;
    return true;
}

bool FirmwareApp::update(uint32_t nowMs) {
    if (!status_.running) {
        return false;
    }

    if (!hardware_.isReady()) {
        enterHardwareFailSafe();
        return false;
    }

    if (
        !rtcReadScheduled_ ||
        nowMs - lastRtcReadMs_ >=
            RTC_READ_INTERVAL_MS
    ) {
        lastRtcReadMs_ = nowMs;
        rtcReadScheduled_ = true;

        if (!timeService_.rtcInitialized()) {
            (void)timeService_.begin();
            cachedLocalTime_ =
                timeService_.current();
        } else {
            cachedLocalTime_ =
                timeService_.now();
        }

        status_.rtcOk =
            timeService_.rtcInitialized();
    }

    core_.update(cachedLocalTime_, nowMs);

    status_.timeValid =
        core_.state().timeValid;

    if (!writeOutputs()) {
        enterHardwareFailSafe();
        return false;
    }

    return true;
}

bool FirmwareApp::isRunning() const {
    return status_.running;
}

const FirmwareStatus& FirmwareApp::status() const {
    return status_;
}

const DeviceConfig& FirmwareApp::config() const {
    return config_;
}

const RuntimeState& FirmwareApp::state() const {
    return core_.state();
}

const LocalTime& FirmwareApp::localTime() const {
    return cachedLocalTime_;
}

uint32_t FirmwareApp::rtcReadAgeMs(uint32_t nowMs) const {
    return rtcReadScheduled_
        ? nowMs - lastRtcReadMs_
        : 0U;
}

FirmwareCommandResult FirmwareApp::setActiveProfileIndex(
    uint8_t profileIndex
) {
    if (profileIndex >= PROFILE_COUNT) {
        return FirmwareCommandResult::Invalid;
    }

    if (!status_.running) {
        return FirmwareCommandResult::Rejected;
    }

    if (config_.activeProfileIndex == profileIndex) {
        return FirmwareCommandResult::NoChange;
    }

    DeviceConfig candidate = config_;
    candidate.activeProfileIndex = profileIndex;

    if (!ConfigValidator::validate(candidate)) {
        return FirmwareCommandResult::Invalid;
    }

    if (!status_.storageOk || !storage_.save(candidate)) {
        return FirmwareCommandResult::StorageFailure;
    }

    config_ = candidate;
    status_.configSource = ConfigSource::Nvs;
    return FirmwareCommandResult::Applied;
}

FirmwareCommandResult FirmwareApp::commandMode(
    OperatingMode requestedMode
) {
    if (
        requestedMode != OperatingMode::Normal &&
        requestedMode != OperatingMode::Service &&
        requestedMode != OperatingMode::Off
    ) {
        return FirmwareCommandResult::Invalid;
    }

    if (!status_.running) {
        return FirmwareCommandResult::Rejected;
    }

    const RuntimeState before = core_.state();
    if (reflectsMode(before, requestedMode)) {
        return FirmwareCommandResult::NoChange;
    }

    switch (requestedMode) {
        case OperatingMode::Normal:
            if (before.mode == OperatingMode::Off) {
                core_.exitOff();
            } else {
                core_.exitService();
            }
            break;
        case OperatingMode::Service:
            core_.enterService();
            break;
        case OperatingMode::Off:
            core_.enterOff();
            break;
        default:
            return FirmwareCommandResult::Invalid;
    }

    return reflectsMode(core_.state(), requestedMode)
        ? FirmwareCommandResult::Applied
        : FirmwareCommandResult::Rejected;
}

FirmwareCommandResult FirmwareApp::commandManual(
    const ChannelLevels& levels,
    uint16_t timeoutMinutes,
    uint32_t nowMs
) {
    if (
        !validManualLevels(levels) ||
        !validManualTimeout(timeoutMinutes)
    ) {
        return FirmwareCommandResult::Invalid;
    }

    if (!status_.running) {
        return FirmwareCommandResult::Rejected;
    }

    if (core_.state().mode == OperatingMode::Manual) {
        core_.setManualLevels(levels);
        return FirmwareCommandResult::Applied;
    }

    core_.enterManual(levels, timeoutMinutes, nowMs);
    return core_.state().mode == OperatingMode::Manual
        ? FirmwareCommandResult::Applied
        : FirmwareCommandResult::Rejected;
}

FirmwareCommandResult FirmwareApp::exitManual() {
    if (!status_.running) {
        return FirmwareCommandResult::Rejected;
    }

    if (core_.state().mode != OperatingMode::Manual) {
        return FirmwareCommandResult::Rejected;
    }

    core_.exitManual();
    return core_.state().mode != OperatingMode::Manual
        ? FirmwareCommandResult::Applied
        : FirmwareCommandResult::Rejected;
}

LumaCore& FirmwareApp::core() {
    return core_;
}

bool FirmwareApp::writeOutputs() {
    if (!hardware_.isReady()) {
        return false;
    }

    const RuntimeState& runtime =
        core_.state();

    if (
        !runtime.timeValid ||
        runtime.mode == OperatingMode::Off
    ) {
        return hardware_.allChannelsOff();
    }

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        if (
            !hardware_.isChannelAvailable(channel) ||
            !hardware_.setChannelPercent(
                channel,
                runtime.actualLevels.value[channel]
            )
        ) {
            return false;
        }
    }

    return true;
}

void FirmwareApp::enterHardwareFailSafe() {
    status_.hardwareOk = false;
    status_.running = false;

    (void)hardware_.allChannelsOff();
}

} // namespace LumaSense
