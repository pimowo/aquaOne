#pragma once

#include <stdint.h>

#include "../core/LumaCore.h"
#include "../hardware/HardwareInterface.h"
#include "../storage/StorageService.h"
#include "../time/TimeService.h"

namespace LumaSense {

enum class ConfigSource : uint8_t {
    Defaults = 0,
    Nvs
};

enum class FirmwareCommandResult : uint8_t {
    Applied = 0,
    NoChange,
    Invalid,
    Rejected,
    StorageFailure
};

struct FirmwareStatus {
    bool hardwareOk = false;
    bool rtcOk = false;
    bool storageOk = false;
    bool configValid = false;
    bool coreOk = false;
    bool timeValid = false;
    bool running = false;

    ConfigSource configSource =
        ConfigSource::Defaults;
};

class FirmwareApp {
public:
    FirmwareApp(
        HardwareInterface& hardware,
        StorageService& storage,
        TimeService& timeService
    );

    bool begin(uint32_t nowMs = 0);
    bool update(uint32_t nowMs);

    bool isRunning() const;

    const FirmwareStatus& status() const;
    const DeviceConfig& config() const;
    const RuntimeState& state() const;
    const LocalTime& localTime() const;
    uint32_t rtcReadAgeMs(uint32_t nowMs) const;

    FirmwareCommandResult setActiveProfileIndex(uint8_t profileIndex);
    FirmwareCommandResult commandMode(OperatingMode mode);
    FirmwareCommandResult commandManual(
        const ChannelLevels& levels,
        uint16_t timeoutMinutes,
        uint32_t nowMs
    );
    FirmwareCommandResult exitManual();

    LumaCore& core();

private:
    HardwareInterface& hardware_;
    StorageService& storage_;
    TimeService& timeService_;

    DeviceConfig config_ {};
    LumaCore core_;
    FirmwareStatus status_ {};

    LocalTime cachedLocalTime_ {};
    uint32_t lastRtcReadMs_ = 0;
    bool rtcReadScheduled_ = false;

    bool writeOutputs();
    void enterHardwareFailSafe();
};

} // namespace LumaSense
