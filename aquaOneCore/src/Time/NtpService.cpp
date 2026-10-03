#include "AquaCore/Time/NtpService.h"

#include <cstring>
#include <time.h>

#if defined(ARDUINO)
#include <esp_sntp.h>
#endif

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

#if defined(ARDUINO)
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

    if (time(&epoch) < static_cast<time_t>(0)) {
        return NtpBackendResult::Failure;
    }

    struct tm utcTime {};

    if (gmtime_r(&epoch, &utcTime) == nullptr) {
        return NtpBackendResult::Failure;
    }

    const int64_t year = static_cast<int64_t>(utcTime.tm_year) + 1900LL;
    if (year < 2000LL || year > 2199LL) {
        return NtpBackendResult::Failure;
    }

    utc.year = static_cast<uint16_t>(year);
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
#endif

void NtpService::LegacyMillisClock::observe(uint32_t nowMs) {
    if (!hasObservation_) {
        nowMs_ = nowMs;
        hasObservation_ = true;
    } else {
        nowMs_ += static_cast<uint32_t>(nowMs - lastMs_);
    }
    lastMs_ = nowMs;
}

uint64_t NtpService::LegacyMillisClock::expanded(uint32_t nowMs) const {
    return hasObservation_
        ? nowMs_ + static_cast<uint32_t>(nowMs - lastMs_)
        : static_cast<uint64_t>(nowMs);
}

NtpService::NtpService(
    RtcService& rtc,
    NtpBackend& backend
) :
    rtc_(rtc),
    backend_(backend),
    clock_(legacyClock_),
    legacyTiming_(true) {
}

NtpService::NtpService(
    RtcService& rtc,
    NtpBackend& backend,
    const MonotonicClock& clock
) :
    rtc_(rtc),
    backend_(backend),
    clock_(clock),
    legacyTiming_(false) {
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
    if (legacyTiming_) {
        legacyClock_.observe(nowMs);
    }
    return beginAt(config, clock_.nowMilliseconds());
}

bool NtpService::beginAt(
    const NtpConfig& config,
    uint64_t nowMs
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
    if (legacyTiming_) {
        legacyClock_.observe(nowMs);
    }
    return requestSyncAt(wifiAvailable, clock_.nowMilliseconds());
}

bool NtpService::requestSync(bool wifiAvailable) {
    return requestSyncAt(wifiAvailable, clock_.nowMilliseconds());
}

bool NtpService::requestSyncAt(
    bool wifiAvailable,
    uint64_t nowMs
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
    if (legacyTiming_) {
        legacyClock_.observe(nowMs);
    }
    return requestPeriodicSync(wifiAvailable);
}

bool NtpService::requestPeriodicSync(bool wifiAvailable) {
    const uint64_t nowMs = clock_.nowMilliseconds();
    if (!isPeriodicSyncDueAt(nowMs)) {
        return false;
    }

    return requestSyncAt(wifiAvailable, nowMs);
}

void NtpService::update(
    bool wifiAvailable,
    uint32_t nowMs
) {
    if (legacyTiming_) {
        legacyClock_.observe(nowMs);
    }
    update(wifiAvailable);
}

void NtpService::update(bool wifiAvailable) {
    updateAt(wifiAvailable, clock_.nowMilliseconds());
}

void NtpService::updateAt(
    bool wifiAvailable,
    uint64_t nowMs
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
    return isPeriodicSyncDueAt(timeAt(nowMs));
}

bool NtpService::isPeriodicSyncDue() const {
    return isPeriodicSyncDueAt(clock_.nowMilliseconds());
}

bool NtpService::isPeriodicSyncDueAt(uint64_t nowMs) const {
    return
        initialized_ &&
        !syncInProgress_ &&
        nowMs - periodicAnchorMs_ >= syncIntervalMs_;
}

bool NtpService::lastSuccessfulSyncAgeMs(
    uint32_t nowMs,
    uint32_t& ageMs
) const {
    uint64_t fullAge = 0U;
    if (!hasSuccessfulSync_) {
        ageMs = 0U;
        return false;
    }
    fullAge = timeAt(nowMs) - lastSuccessfulSyncMs_;
    ageMs = static_cast<uint32_t>(fullAge);
    return true;
}

bool NtpService::lastSuccessfulSyncAgeMs(uint64_t& ageMs) const {
    if (!hasSuccessfulSync_) {
        ageMs = 0U;
        return false;
    }

    ageMs = clock_.nowMilliseconds() - lastSuccessfulSyncMs_;
    return true;
}

uint64_t NtpService::timeAt(uint32_t nowMs) const {
    return legacyTiming_
        ? legacyClock_.expanded(nowMs)
        : clock_.nowMilliseconds();
}

void NtpService::finishAttempt(
    bool fetchSucceeded,
    bool rtcUpdated,
    uint64_t nowMs
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
