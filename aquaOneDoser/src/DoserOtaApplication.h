#pragma once

#include <stddef.h>
#include <stdint.h>

#include <AquaCore/Web/PublishedSnapshot.h>

#include "StreamingUploadBridge.h"

class DoserOtaAuthority {
public:
    virtual ~DoserOtaAuthority() = default;
    virtual uint32_t nowMs() const = 0;
    virtual size_t availableFirmwareSpace() const = 0;
    virtual bool beginFirmwareUpdate(size_t size) = 0;
    virtual size_t writeFirmware(const uint8_t* data, size_t length) = 0;
    virtual bool endFirmwareUpdate() = 0;
    virtual void abortFirmwareUpdate() = 0;
    virtual const char* firmwareError() const = 0;
    virtual void stopPumps() = 0;
    virtual void setOtaInProgress(bool active) = 0;
};

// HTTPD reads time through this separate narrow interface; it never calls
// mutable Application state or the Update authority from a route callback.
class DoserOtaClock {
public:
    virtual ~DoserOtaClock() = default;
    virtual uint32_t nowMs() const = 0;
};

class DoserOtaRestartScheduler {
public:
    virtual ~DoserOtaRestartScheduler() = default;
    virtual bool isRestartPending() const = 0;
    virtual bool scheduleOtaRestart() = 0;
};

struct DoserOtaCapacity {
    uint32_t availableBytes = 0U;
    bool available = false;
};
using DoserOtaCapacitySnapshot = AquaCore::Web::PublishedSnapshot<DoserOtaCapacity>;

class DoserOtaApplication {
public:
    enum class State : uint8_t {
        Idle, Starting, Receiving, Finalizing, SucceededAwaitingRestart, Aborting, Failed
    };
    DoserOtaApplication(StreamingUploadBridge& bridge, DoserOtaAuthority& authority,
                        DoserOtaRestartScheduler& restart,
                        DoserOtaCapacitySnapshot& capacity)
        : bridge_(bridge), authority_(authority), restart_(restart), capacity_(capacity) {}
    bool publishCapacity();
    bool processOne();
    void serviceRestartWatchdog();
    bool blocksMqttService() const;
    State state() const { return state_; } // Application context only.
    uint32_t receivedBytes() const { return received_; }
    bool restartArmed() const { return restartArmed_; }

private:
    DoserOtaResult execute(const UploadCommand& command, const uint8_t* bytes);
    DoserOtaResult fail(DoserOtaError error, const char* diagnostic = nullptr);
    void cleanup();
    static void copyDiagnostic(char out[97], const char* text);

    StreamingUploadBridge& bridge_;
    DoserOtaAuthority& authority_;
    DoserOtaRestartScheduler& restart_;
    DoserOtaCapacitySnapshot& capacity_;
    State state_ = State::Idle;
    uint64_t generation_ = 0U;
    uint32_t expected_ = 0U;
    uint32_t received_ = 0U;
    uint32_t finalizedAt_ = 0U;
    bool updateBegun_ = false;
    bool otaGuard_ = false;
    bool restartArmed_ = false;
};
