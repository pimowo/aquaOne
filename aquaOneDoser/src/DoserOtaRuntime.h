#pragma once

#include "DoserOtaApplication.h"

class PumpDriver;
class SchedulerManager;

// Narrow Application-side Update and scheduler adapter.
class DoserOtaRuntime final : public DoserOtaAuthority, public DoserOtaClock {
public:
    DoserOtaRuntime(PumpDriver& pumps, SchedulerManager& scheduler)
        : pumps_(pumps), scheduler_(scheduler) {}
    uint32_t nowMs() const override;
    size_t availableFirmwareSpace() const override;
    bool beginFirmwareUpdate(size_t size) override;
    size_t writeFirmware(const uint8_t* data, size_t length) override;
    bool endFirmwareUpdate() override;
    void abortFirmwareUpdate() override;
    const char* firmwareError() const override;
    void stopPumps() override;
    void setOtaInProgress(bool active) override;

private:
    PumpDriver& pumps_;
    SchedulerManager& scheduler_;
};
