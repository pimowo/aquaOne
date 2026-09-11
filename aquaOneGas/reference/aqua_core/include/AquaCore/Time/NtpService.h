#pragma once

#include <stdint.h>

#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/TimeConstants.h"
#include "AquaCore/Time/TimeTypes.h"

namespace AquaCore {
namespace Time {

enum class NtpBackendResult : uint8_t {
    Pending = 0,
    Success,
    Failure
};

class NtpBackend {
public:
    virtual ~NtpBackend() = default;

    virtual bool start(
        const char* const servers[NTP_MAX_SERVERS],
        uint8_t serverCount
    ) = 0;

    virtual NtpBackendResult poll(
        UtcDateTime& utc
    ) = 0;

    virtual void stop() = 0;
};

class EspNtpBackend final : public NtpBackend {
public:
    bool start(
        const char* const servers[NTP_MAX_SERVERS],
        uint8_t serverCount
    ) override;

    NtpBackendResult poll(
        UtcDateTime& utc
    ) override;

    void stop() override;
};

struct NtpConfig {
    const char* servers[NTP_MAX_SERVERS] {};
    uint8_t serverCount = 0U;
    uint32_t timeoutMs = NTP_SYNC_TIMEOUT_MS;
    uint32_t syncIntervalMs = NTP_SYNC_INTERVAL_MS;
};

class NtpService {
public:
    NtpService(
        RtcService& rtc,
        NtpBackend& backend
    );

    static NtpConfig defaultConfig();

    bool begin(uint32_t nowMs = 0U);

    bool begin(
        const NtpConfig& config,
        uint32_t nowMs = 0U
    );

    bool requestSync(
        bool wifiAvailable,
        uint32_t nowMs
    );

    bool requestPeriodicSync(
        bool wifiAvailable,
        uint32_t nowMs
    );

    void update(
        bool wifiAvailable,
        uint32_t nowMs
    );

    bool isInitialized() const;
    bool isSyncInProgress() const;
    bool hasSyncResult() const;
    bool lastSyncSucceeded() const;
    bool isPeriodicSyncDue(uint32_t nowMs) const;

    bool lastSuccessfulSyncAgeMs(
        uint32_t nowMs,
        uint32_t& ageMs
    ) const;

private:
    RtcService& rtc_;
    NtpBackend& backend_;

    char serverNames_[NTP_MAX_SERVERS]
        [NTP_SERVER_NAME_CAPACITY] {};
    uint8_t serverCount_ = 0U;

    uint32_t timeoutMs_ = NTP_SYNC_TIMEOUT_MS;
    uint32_t syncIntervalMs_ = NTP_SYNC_INTERVAL_MS;

    uint32_t attemptStartedMs_ = 0U;
    uint32_t periodicAnchorMs_ = 0U;
    uint32_t lastSuccessfulSyncMs_ = 0U;

    bool initialized_ = false;
    bool syncInProgress_ = false;
    bool hasSyncResult_ = false;
    bool lastSyncSucceeded_ = false;
    bool hasSuccessfulSync_ = false;

    void finishAttempt(
        bool succeeded,
        uint32_t nowMs
    );
};

} // namespace Time
} // namespace AquaCore