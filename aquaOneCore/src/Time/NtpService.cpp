#include "AquaCore/Time/NtpService.h"

#include <cstring>
#include <time.h>

#include <esp_sntp.h>

namespace AquaCore {
namespace Time {
namespace {

constexpr char DEFAULT_NTP_SERVER_1[] = "pool.ntp.org";
constexpr char DEFAULT_NTP_SERVER_2[] = "time.google.com";
constexpr char DEFAULT_NTP_SERVER_3[] = "time.cloudflare.com";

bool copyServerName(
    char destination[NTP_SERVER_NAME_CAPACITY],
    const char* source
) {
    if (source == nullptr) {
        return false;
    }

    const size_t length = std::strlen(source);

    if (
        length == 0U ||
        length >= NTP_SERVER_NAME_CAPACITY
    ) {
        return false;
    }

    std::memcpy(destination, source, length + 1U);
    return true;
}

} // namespace

bool EspNtpBackend::start(
    const char* const servers[NTP_MAX_SERVERS],
    uint8_t serverCount
) {
    if (
        servers == nullptr ||
        serverCount == 0U ||
        serverCount > NTP_MAX_SERVERS
    ) {
        return false;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(
        ESP_SNTP_OPMODE_POLL
    );
    esp_sntp_set_sync_mode(
        SNTP_SYNC_MODE_IMMED
    );
    esp_sntp_set_sync_status(
        SNTP_SYNC_STATUS_RESET
    );

    for (
        uint8_t index = 0U;
        index < NTP_MAX_SERVERS;
        ++index
    ) {
        esp_sntp_setservername(
            index,
            index < serverCount
                ? servers[index]
                : nullptr
        );
    }

    esp_sntp_init();
    return esp_sntp_enabled();
}

NtpBackendResult EspNtpBackend::poll(
    UtcDateTime& utc
) {
    utc = {};

    if (!esp_sntp_enabled()) {
        return NtpBackendResult::Failure;
    }

    if (
        esp_sntp_get_sync_status() !=
        SNTP_SYNC_STATUS_COMPLETED
    ) {
        return NtpBackendResult::Pending;
    }

    time_t epoch = 0;

    if (time(&epoch) < 0) {
        return NtpBackendResult::Failure;
    }

    struct tm utcTime {};

    if (gmtime_r(&epoch, &utcTime) == nullptr) {
        return NtpBackendResult::Failure;
    }

    utc.year = static_cast<uint16_t>(
        utcTime.tm_year + 1900
    );
    utc.month = static_cast<uint8_t>(
        utcTime.tm_mon + 1
    );
    utc.day = static_cast<uint8_t>(utcTime.tm_mday);
    utc.hour = static_cast<uint8_t>(utcTime.tm_hour);
    utc.minute = static_cast<uint8_t>(utcTime.tm_min);
    utc.second = static_cast<uint8_t>(utcTime.tm_sec);

    return NtpBackendResult::Success;
}

void EspNtpBackend::stop() {
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
}

NtpService::NtpService(
    RtcService& rtc,
    NtpBackend& backend
) :
    rtc_(rtc),
    backend_(backend) {
}

NtpConfig NtpService::defaultConfig() {
    NtpConfig config {};
    config.servers[0] = DEFAULT_NTP_SERVER_1;
    config.servers[1] = DEFAULT_NTP_SERVER_2;
    config.servers[2] = DEFAULT_NTP_SERVER_3;
    config.serverCount = NTP_MAX_SERVERS;
    config.timeoutMs = NTP_SYNC_TIMEOUT_MS;
    config.syncIntervalMs = NTP_SYNC_INTERVAL_MS;
    return config;
}

bool NtpService::begin(uint32_t nowMs) {
    return begin(defaultConfig(), nowMs);
}

bool NtpService::begin(
    const NtpConfig& config,
    uint32_t nowMs
) {
    if (syncInProgress_) {
        backend_.stop();
    }

    initialized_ = false;
    syncInProgress_ = false;
    hasSyncResult_ = false;
    lastSyncSucceeded_ = false;
    lastFetchSucceeded_ = false;
    newUtcPending_ = false;
    hasSuccessfulSync_ = false;
    serverCount_ = 0U;

    if (
        config.serverCount == 0U ||
        config.serverCount > NTP_MAX_SERVERS ||
        config.timeoutMs == 0U ||
        config.syncIntervalMs == 0U
    ) {
        return false;
    }

    std::memset(
        serverNames_,
        0,
        sizeof(serverNames_)
    );

    for (
        uint8_t index = 0U;
        index < config.serverCount;
        ++index
    ) {
        if (!copyServerName(
            serverNames_[index],
            config.servers[index]
        )) {
            return false;
        }
    }

    serverCount_ = config.serverCount;
    timeoutMs_ = config.timeoutMs;
    syncIntervalMs_ = config.syncIntervalMs;
    rtcSyncEnabled_ = config.rtcSyncEnabled;
    attemptStartedMs_ = nowMs;
    periodicAnchorMs_ = nowMs;
    lastSuccessfulSyncMs_ = 0U;
    initialized_ = true;
    return true;
}

bool NtpService::requestSync(
    bool wifiAvailable,
    uint32_t nowMs
) {
    if (!initialized_ || syncInProgress_) {
        return false;
    }

    attemptStartedMs_ = nowMs;

    if (!wifiAvailable) {
        hasSyncResult_ = true;
        lastSyncSucceeded_ = false;
        lastFetchSucceeded_ = false;
        periodicAnchorMs_ = nowMs;
        return false;
    }

    const char* servers[NTP_MAX_SERVERS] {};

    for (
        uint8_t index = 0U;
        index < serverCount_;
        ++index
    ) {
        servers[index] = serverNames_[index];
    }

    if (!backend_.start(servers, serverCount_)) {
        backend_.stop();
        hasSyncResult_ = true;
        lastSyncSucceeded_ = false;
        lastFetchSucceeded_ = false;
        periodicAnchorMs_ = nowMs;
        return false;
    }

    syncInProgress_ = true;
    return true;
}

bool NtpService::requestPeriodicSync(
    bool wifiAvailable,
    uint32_t nowMs
) {
    if (!isPeriodicSyncDue(nowMs)) {
        return false;
    }

    return requestSync(wifiAvailable, nowMs);
}

void NtpService::update(
    bool wifiAvailable,
    uint32_t nowMs
) {
    if (!syncInProgress_) {
        return;
    }

    if (
        !wifiAvailable ||
        nowMs - attemptStartedMs_ >= timeoutMs_
    ) {
        finishAttempt(false, false, nowMs);
        return;
    }

    UtcDateTime utc {};
    const NtpBackendResult result =
        backend_.poll(utc);

    if (result == NtpBackendResult::Pending) {
        return;
    }

    if (
        result == NtpBackendResult::Failure ||
        !RtcService::isValidUtc(
            utc.year,
            utc.month,
            utc.day,
            utc.hour,
            utc.minute,
            utc.second
        )
    ) {
        finishAttempt(false, false, nowMs);
        return;
    }

    lastReceivedUtc_ = utc;
    newUtcPending_ = true;

    bool rtcUpdated = false;
    if (rtcSyncEnabled_) {
        rtcUpdated = rtc_.setUtc(
            utc.year,
            utc.month,
            utc.day,
            utc.hour,
            utc.minute,
            utc.second
        );
    }

    finishAttempt(true, rtcUpdated, nowMs);
}

bool NtpService::isInitialized() const {
    return initialized_;
}

bool NtpService::isSyncInProgress() const {
    return syncInProgress_;
}

bool NtpService::hasSyncResult() const {
    return hasSyncResult_;
}

bool NtpService::lastSyncSucceeded() const {
    return hasSyncResult_ && lastSyncSucceeded_;
}

bool NtpService::lastFetchSucceeded() const {
    return hasSyncResult_ && lastFetchSucceeded_;
}

bool NtpService::takeReceivedUtc(UtcDateTime& output) {
    if (!newUtcPending_) {
        return false;
    }

    output = lastReceivedUtc_;
    newUtcPending_ = false;
    return true;
}

bool NtpService::isPeriodicSyncDue(
    uint32_t nowMs
) const {
    return
        initialized_ &&
        !syncInProgress_ &&
        nowMs - periodicAnchorMs_ >= syncIntervalMs_;
}

bool NtpService::lastSuccessfulSyncAgeMs(
    uint32_t nowMs,
    uint32_t& ageMs
) const {
    if (!hasSuccessfulSync_) {
        ageMs = 0U;
        return false;
    }

    ageMs = nowMs - lastSuccessfulSyncMs_;
    return true;
}

void NtpService::finishAttempt(
    bool fetchSucceeded,
    bool rtcUpdated,
    uint32_t nowMs
) {
    backend_.stop();

    syncInProgress_ = false;
    hasSyncResult_ = true;
    lastFetchSucceeded_ = fetchSucceeded;
    lastSyncSucceeded_ = fetchSucceeded && rtcUpdated;
    periodicAnchorMs_ = nowMs;

    if (lastSyncSucceeded_) {
        hasSuccessfulSync_ = true;
        lastSuccessfulSyncMs_ = nowMs;
    }
}

} // namespace Time
} // namespace AquaCore