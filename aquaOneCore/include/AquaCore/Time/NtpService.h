#pragma once

#include <stdint.h>

#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Time/MonotonicClock.h"
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
    bool rtcSyncEnabled = true;
};

class NtpService {
public:
    NtpService(
        RtcService& rtc,
        NtpBackend& backend
    );
    // Borrowed clock is authoritative for the vNext overloads without nowMs.
    NtpService(
        RtcService& rtc,
        NtpBackend& backend,
        const MonotonicClock& clock
    );
    NtpService(const NtpService&) = delete;
    NtpService& operator=(const NtpService&) = delete;
    NtpService(NtpService&&) = delete;
    NtpService& operator=(NtpService&&) = delete;

    static NtpConfig defaultConfig();

    // Legacy nowMs methods feed a per-instance millis() rollover extension.
    // It cannot detect a gap of one full uint32_t cycle between observations.
    // With the injected clock, nowMs is ignored.
    bool begin(uint32_t nowMs = 0U);

    bool begin(
        const NtpConfig& config,
        uint32_t nowMs = 0U
    );

    bool requestSync(
        bool wifiAvailable,
        uint32_t nowMs
    );
    bool requestSync(bool wifiAvailable);

    bool requestPeriodicSync(
        bool wifiAvailable,
        uint32_t nowMs
    );
    bool requestPeriodicSync(bool wifiAvailable);

    void update(
        bool wifiAvailable,
        uint32_t nowMs
    );
    void update(bool wifiAvailable);

    bool isInitialized() const;
    bool isSyncInProgress() const;
    bool hasSyncResult() const;
    bool lastSyncSucceeded() const;
    bool lastFetchSucceeded() const;
    bool takeReceivedUtc(UtcDateTime& output);
    bool isPeriodicSyncDue(uint32_t nowMs) const;
    bool isPeriodicSyncDue() const;

    bool lastSuccessfulSyncAgeMs(
        uint32_t nowMs,
        uint32_t& ageMs
    ) const;
    bool lastSuccessfulSyncAgeMs(uint64_t& ageMs) const;

private:
    class LegacyMillisClock final : public MonotonicClock {
    public:
        uint64_t nowMilliseconds() const override { return nowMs_; }
        void observe(uint32_t nowMs);
        uint64_t expanded(uint32_t nowMs) const;

    private:
        uint64_t nowMs_ = 0U;
        uint32_t lastMs_ = 0U;
        bool hasObservation_ = false;
    };

    RtcService& rtc_;
    NtpBackend& backend_;
    LegacyMillisClock legacyClock_ {};
    const MonotonicClock& clock_;
    bool legacyTiming_;

    char serverNames_[NTP_MAX_SERVERS]
        [NTP_SERVER_NAME_CAPACITY] {};
    uint8_t serverCount_ = 0U;

    uint32_t timeoutMs_ = NTP_SYNC_TIMEOUT_MS;
    uint32_t syncIntervalMs_ = NTP_SYNC_INTERVAL_MS;

    uint64_t attemptStartedMs_ = 0U;
    uint64_t periodicAnchorMs_ = 0U;
    uint64_t lastSuccessfulSyncMs_ = 0U;

    UtcDateTime lastReceivedUtc_ {};
    bool newUtcPending_ = false;
    bool lastFetchSucceeded_ = false;

    bool initialized_ = false;
    bool syncInProgress_ = false;
    bool hasSyncResult_ = false;
    bool lastSyncSucceeded_ = false;
    bool hasSuccessfulSync_ = false;
    bool rtcSyncEnabled_ = true;

    void finishAttempt(
        bool fetchSucceeded,
        bool rtcUpdated,
        uint64_t nowMs
    );
    uint64_t timeAt(uint32_t nowMs) const;
    bool beginAt(const NtpConfig& config, uint64_t nowMs);
    bool requestSyncAt(bool wifiAvailable, uint64_t nowMs);
    bool isPeriodicSyncDueAt(uint64_t nowMs) const;
    void updateAt(bool wifiAvailable, uint64_t nowMs);
};

} // namespace Time
} // namespace AquaCore
