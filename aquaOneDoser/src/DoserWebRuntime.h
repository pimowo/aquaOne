#pragma once

#include <stddef.h>
#include <stdint.h>

class DiagnosticsManager;
class MqttManager;
class PumpDriver;
class SchedulerManager;
class TimeManager;
class WiFiManager;

class WebManagerRuntime {
public:
    virtual ~WebManagerRuntime() = default;

    virtual unsigned long nowMs() const = 0;
    virtual size_t availableFirmwareSpace() const = 0;
    virtual bool beginFirmwareUpdate(size_t expectedSize) = 0;
    virtual size_t writeFirmware(const uint8_t* data, size_t length) = 0;
    virtual bool endFirmwareUpdate() = 0;
    virtual void abortFirmwareUpdate() = 0;
    virtual const char* firmwareError() const = 0;
    virtual void stopPumps() = 0;
    virtual void setOtaInProgress(bool inProgress) = 0;
    virtual void serviceDuringUpload() = 0;
    virtual void restartDevice() = 0;
};

class DoserWebRuntime final : public WebManagerRuntime {
public:
    DoserWebRuntime(TimeManager& timeManager, MqttManager& mqttManager,
                    DiagnosticsManager& diagnosticsManager,
                    SchedulerManager& schedulerManager,
                    WiFiManager& wifiManager, PumpDriver& pumpDriver);

    unsigned long nowMs() const override;
    size_t availableFirmwareSpace() const override;
    bool beginFirmwareUpdate(size_t expectedSize) override;
    size_t writeFirmware(const uint8_t* data, size_t length) override;
    bool endFirmwareUpdate() override;
    void abortFirmwareUpdate() override;
    const char* firmwareError() const override;
    void stopPumps() override;
    void setOtaInProgress(bool inProgress) override;
    void serviceDuringUpload() override;
    void restartDevice() override;

private:
    TimeManager& time_;
    MqttManager& mqtt_;
    DiagnosticsManager& diagnostics_;
    SchedulerManager& scheduler_;
    WiFiManager& wifi_;
    PumpDriver& driver_;
};
