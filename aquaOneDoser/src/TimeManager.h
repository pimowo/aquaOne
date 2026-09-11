#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <sys/time.h>

class TimeManager {
public:
    bool begin();
    void loop();

    bool isRtcOk() const;
    bool isNtpSynced() const;
    uint32_t getLastNtpSyncTimestamp() const;

    DateTime getUtcTime() const;
    tm getLocalTime() const;

    void printStatus();

private:
    static constexpr unsigned long NTP_RETRY_INTERVAL_MS = 60000UL;
    static constexpr uint32_t NTP_SYNC_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;

    RTC_DS3231 rtc;
    DateTime cachedUtc{static_cast<uint32_t>(0)};
    unsigned long cachedAtMillis = 0;
    unsigned long lastRtcReadAttempt = 0;
    uint8_t consecutiveRtcFailures = 0;
    uint8_t consecutiveRtcSuccesses = 0;
    bool rtcFoundAtBoot = false;
    bool rtcOk = false;
    bool ntpSynced = false;
    volatile bool ntpUpdatePending = false;
    unsigned long lastNtpAttempt = 0;
    uint32_t lastNtpSyncTimestamp = 0;

    static TimeManager* instance;
    static void onNtpSync(struct timeval* timeInfo);

    void startNtpSync();
    void handleNtpUpdate();
    void updateRtcFromSystem();
    void pollRtc(bool initialRead = false);
    bool readRtc(DateTime& value);
};
