#pragma once

#include <Arduino.h>
#include <time.h>

#include <AquaCore/Time/RtcService.h>
#include <AquaCore/Time/ResilientTimeService.h>
#include <AquaCore/Time/NtpService.h>
#include <AquaCore/Time/EuropeWarsawTimeService.h>
#include <AquaCore/Time/RtcBus.h>

class TimeManager {
public:
    TimeManager();
    ~TimeManager();

    bool begin();
    void loop();

    bool isTimeEnabled() const;
    bool isRtcConfigured() const;
    bool isRtcOk() const;
    bool isTimeValid() const;
    bool isNtpConfigured() const;
    bool isNtpSynced() const;
    uint32_t getLastNtpSyncTimestamp() const;

    uint32_t getUtcTimestamp() const;
    tm getLocalTime() const;

    void printStatus();

private:
    class DoserRtcBus;
    DoserRtcBus* bus_ = nullptr;
    AquaCore::Time::RtcService* rtc_ = nullptr;
    AquaCore::Time::ResilientTimeService* resilientTime_ = nullptr;
    AquaCore::Time::EspNtpBackend* ntpBackend_ = nullptr;
    AquaCore::Time::NtpService* ntpService_ = nullptr;

    bool timeEnabled_ = true;
    bool rtcEnabled_ = true;
    bool ntpEnabled_ = true;

    unsigned long lastRtcPoll_ = 0;
    unsigned long lastNtpRequestAttempt_ = 0;
    bool wifiWasAvailable_ = false;
    bool ntpSynced_ = false;
    uint32_t lastNtpSyncTimestamp_ = 0;
    bool rtcWarningLogged_ = false;
    bool timeValidLogged_ = false;
};
