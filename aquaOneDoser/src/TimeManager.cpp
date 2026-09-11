#include "TimeManager.h"
#include "app_config.h"

#include <Wire.h>
#include <WiFi.h>

class TimeManager::DoserRtcBus final : public AquaCore::Time::RtcBus {
public:
    bool begin(int sdaPin, int sclPin) override {
        return Wire.begin(sdaPin, sclPin);
    }

    bool readRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        uint8_t* buffer,
        size_t length
    ) override {
        Wire.beginTransmission(deviceAddress);

        if (Wire.write(startRegister) != 1U) {
            return false;
        }

        if (Wire.endTransmission(false) != 0U) {
            return false;
        }

        const size_t received = Wire.requestFrom(deviceAddress, length);
        if (received != length) {
            return false;
        }

        for (size_t index = 0U; index < length; ++index) {
            const int value = Wire.read();
            if (value < 0) {
                return false;
            }
            buffer[index] = static_cast<uint8_t>(value);
        }

        return true;
    }

    bool writeRegisters(
        uint8_t deviceAddress,
        uint8_t startRegister,
        const uint8_t* buffer,
        size_t length
    ) override {
        Wire.beginTransmission(deviceAddress);

        if (Wire.write(startRegister) != 1U) {
            return false;
        }

        for (size_t index = 0U; index < length; ++index) {
            if (Wire.write(buffer[index]) != 1U) {
                return false;
            }
        }

        return Wire.endTransmission() == 0U;
    }
};

namespace {

constexpr uint32_t MIN_VALID_TIMESTAMP = 1700000000UL;

constexpr int daysFromCivil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

uint32_t localTimeToEpoch(const AquaCore::Time::LocalTime& time) {
    if (!time.valid || time.year < 1970U) {
        return 0U;
    }

    const int days = daysFromCivil(time.year, time.month, time.day);
    if (days < 0) {
        return 0U;
    }

    const uint64_t seconds = static_cast<uint64_t>(days) * 86400ULL +
                             static_cast<uint64_t>(time.hour) * 3600ULL +
                             static_cast<uint64_t>(time.minute) * 60ULL +
                             static_cast<uint64_t>(time.second);

    return seconds <= UINT32_MAX ? static_cast<uint32_t>(seconds) : 0U;
}

int dayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
    if (month < 3U) {
        month += 12U;
        --year;
    }

    const uint16_t k = year % 100U;
    const uint16_t j = year / 100U;

    const uint16_t h = (day + (13U * (month + 1U)) / 5U + k + k / 4U + j / 4U + 5U * j) % 7U;
    return static_cast<int>((h + 6U) % 7U); // 0 = Sunday, 1 = Monday...
}

} // namespace

TimeManager::TimeManager()
    : timeEnabled_(TIME_MODULE_ENABLED),
      rtcEnabled_(RTC_MODULE_ENABLED),
      ntpEnabled_(NTP_MODULE_ENABLED) {
}

TimeManager::~TimeManager() {
    delete ntpService_;
    delete ntpBackend_;
    delete resilientTime_;
    delete rtc_;
    delete bus_;
}

bool TimeManager::begin() {
    if (!timeEnabled_) {
        Serial.println("[TIME] Moduł czasu wyłączony w konfiguracji");
        return true;
    }

    bus_ = new DoserRtcBus();

    AquaCore::Time::RtcConfig rtcCfg {};
    rtcCfg.sdaPin = PIN_I2C_SDA;
    rtcCfg.sclPin = PIN_I2C_SCL;
    rtcCfg.i2cAddress = 0x68U;

    rtc_ = new AquaCore::Time::RtcService(*bus_, rtcCfg);

    AquaCore::Time::ResilientTimeConfig resilientCfg {};
    resilientCfg.rtcEnabled = rtcEnabled_;
    resilientCfg.failureThreshold = RTC_FAILURE_THRESHOLD;
    resilientCfg.recoveryThreshold = RTC_RECOVERY_THRESHOLD;
    resilientCfg.unhealthyProbeIntervalMs = 30000U;

    resilientTime_ = new AquaCore::Time::ResilientTimeService(*rtc_, resilientCfg);

    const uint32_t nowMs = millis();

    if (rtcEnabled_) {
        Wire.setTimeOut(100);
        const bool rtcOk = resilientTime_->begin(nowMs);
        if (rtcOk) {
            Serial.println("[RTC] DS3231 OK");
            rtcWarningLogged_ = false;
        } else {
            Serial.println("[RTC] DS3231 nie odpowiada (przejście w probe co 30 s)");
            rtcWarningLogged_ = true;
        }
    } else {
        resilientTime_->begin(nowMs);
        Serial.println("[RTC] DS3231 wyłączony w konfiguracji sprzętowej (NTP-only mode)");
        rtcWarningLogged_ = false;
    }

    if (ntpEnabled_) {
        ntpBackend_ = new AquaCore::Time::EspNtpBackend();
        ntpService_ = new AquaCore::Time::NtpService(*rtc_, *ntpBackend_);
        AquaCore::Time::NtpConfig ntpCfg = AquaCore::Time::NtpService::defaultConfig();
        ntpCfg.rtcSyncEnabled = rtcEnabled_;
        ntpService_->begin(ntpCfg, nowMs);
    }

    lastRtcPoll_ = nowMs;
    return isTimeValid() || (rtcEnabled_ && isRtcOk()) || ntpEnabled_;
}

void TimeManager::loop() {
    if (!timeEnabled_) {
        return;
    }

    const unsigned long nowMs = millis();

    if (rtcEnabled_ && resilientTime_ != nullptr) {
        if (resilientTime_->isRtcHealthy()) {
            if (nowMs - lastRtcPoll_ >= RTC_READ_INTERVAL_MS) {
                lastRtcPoll_ = nowMs;
                resilientTime_->poll(nowMs);
                if (!resilientTime_->isRtcHealthy() && !rtcWarningLogged_) {
                    Serial.println("[RTC] DS3231 utracony (przejście w probe co 30 s)");
                    rtcWarningLogged_ = true;
                }
            }
        } else {
            if (resilientTime_->isProbeDue(nowMs)) {
                lastRtcPoll_ = nowMs;
                resilientTime_->poll(nowMs);
                if (resilientTime_->isRtcHealthy()) {
                    Serial.println("[RTC] DS3231 odzyskany - wznowiono standardowy polling");
                    rtcWarningLogged_ = false;
                }
            }
        }
    }

    const bool wifiAvailable = (WiFi.status() == WL_CONNECTED);

    if (ntpEnabled_ && ntpService_ != nullptr) {
        // 1. Zmiana stanu sieci (pojawienie się Wi-Fi) -> natychmiastowe żądanie synchronizacji
        if (wifiAvailable && !wifiWasAvailable_) {
            Serial.println("[NTP] Wykryto połączenie Wi-Fi - żądanie synchronizacji NTP");
            if (ntpService_->requestSync(true, nowMs)) {
                lastNtpRequestAttempt_ = nowMs;
            }
        }
        // 2. Jeśli Wi-Fi jest dostępne, ale jeszcze nie zsynchronizowano NTP (lub czas jest niepoprawny),
        // ponawiaj żądanie co 10 s
        else if (wifiAvailable && (!ntpSynced_ || !resilientTime_->isValid())) {
            if (!ntpService_->isSyncInProgress() && (nowMs - lastNtpRequestAttempt_ >= 10000UL)) {
                lastNtpRequestAttempt_ = nowMs;
                Serial.println("[NTP] Ponawianie żądania synchronizacji NTP...");
                ntpService_->requestSync(true, nowMs);
            }
        }
        // 3. Po udanej synchronizacji - okresowa synchronizacja według harmonogramu NtpService
        else if (wifiAvailable) {
            ntpService_->requestPeriodicSync(true, nowMs);
        }

        const bool wasInProgress = ntpService_->isSyncInProgress();

        ntpService_->update(wifiAvailable, nowMs);

        // Obsługa zakończenia próby, która nie powiodła się
        if (wasInProgress && !ntpService_->isSyncInProgress() && !ntpService_->lastFetchSucceeded()) {
            Serial.println("[NTP] Próba synchronizacji nie powiodła się (oczekiwanie na ponowienie)");
        }

        AquaCore::Time::UtcDateTime fetchedUtc {};
        if (ntpService_->takeReceivedUtc(fetchedUtc)) {
            AquaCore::Time::LocalTime localUtc {};
            localUtc.valid = true;
            localUtc.year = fetchedUtc.year;
            localUtc.month = fetchedUtc.month;
            localUtc.day = fetchedUtc.day;
            localUtc.hour = fetchedUtc.hour;
            localUtc.minute = fetchedUtc.minute;
            localUtc.second = fetchedUtc.second;
            localUtc.minuteOfDay = static_cast<uint16_t>(fetchedUtc.hour * 60U + fetchedUtc.minute);

            if (resilientTime_ != nullptr) {
                resilientTime_->syncUtc(localUtc, nowMs);
            }

            ntpSynced_ = true;
            lastNtpSyncTimestamp_ = localTimeToEpoch(localUtc);
            Serial.printf("[NTP] Synchronizacja OK: %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                          fetchedUtc.year, fetchedUtc.month, fetchedUtc.day,
                          fetchedUtc.hour, fetchedUtc.minute, fetchedUtc.second);
            if (!timeValidLogged_) {
                Serial.println("[TIME] Czas systemowy poprawny (zsynchronizowany z NTP)");
                timeValidLogged_ = true;
            }
        }

        wifiWasAvailable_ = wifiAvailable;
    }
}

bool TimeManager::isTimeEnabled() const {
    return timeEnabled_;
}

bool TimeManager::isRtcConfigured() const {
    return rtcEnabled_;
}

bool TimeManager::isRtcOk() const {
    return timeEnabled_ && rtcEnabled_ && resilientTime_ != nullptr && resilientTime_->isRtcHealthy();
}

bool TimeManager::isTimeValid() const {
    return timeEnabled_ && resilientTime_ != nullptr && resilientTime_->isValid();
}

bool TimeManager::isNtpConfigured() const {
    return ntpEnabled_;
}

bool TimeManager::isNtpSynced() const {
    return timeEnabled_ && ntpEnabled_ && ntpSynced_;
}

uint32_t TimeManager::getLastNtpSyncTimestamp() const {
    return lastNtpSyncTimestamp_;
}

uint32_t TimeManager::getUtcTimestamp() const {
    if (!isTimeValid()) {
        return 0U;
    }

    const AquaCore::Time::LocalTime utc = resilientTime_->now(millis());
    const uint32_t stamp = localTimeToEpoch(utc);
    return stamp >= MIN_VALID_TIMESTAMP ? stamp : 0U;
}

tm TimeManager::getLocalTime() const {
    tm result {};
    if (!isTimeValid()) {
        return result;
    }

    const AquaCore::Time::LocalTime utc = resilientTime_->now(millis());
    if (!utc.valid) {
        return result;
    }

    const AquaCore::Time::LocalTime warsaw = AquaCore::Time::EuropeWarsawTimeService::convertUtcToWarsaw(utc);
    if (!warsaw.valid) {
        return result;
    }

    result.tm_year = static_cast<int>(warsaw.year) - 1900;
    result.tm_mon = static_cast<int>(warsaw.month) - 1;
    result.tm_mday = static_cast<int>(warsaw.day);
    result.tm_hour = static_cast<int>(warsaw.hour);
    result.tm_min = static_cast<int>(warsaw.minute);
    result.tm_sec = static_cast<int>(warsaw.second);
    result.tm_wday = dayOfWeek(warsaw.year, warsaw.month, warsaw.day);
    result.tm_isdst = -1;

    return result;
}

void TimeManager::printStatus() {
    if (!timeEnabled_) {
        Serial.println("[TIME] Moduł czasu: WYŁĄCZONY");
        return;
    }

    const char* rtcStr = !rtcEnabled_ ? "DISABLED" : (isRtcOk() ? "OK" : "ERROR");
    const char* ntpStr = !ntpEnabled_ ? "DISABLED" : (isNtpSynced() ? "OK" : "BRAK");

    if (!isTimeValid()) {
        Serial.printf("[TIME] RTC: %s | Time: INVALID | NTP: %s\n", rtcStr, ntpStr);
        return;
    }

    const uint32_t nowMs = millis();
    const AquaCore::Time::LocalTime utc = resilientTime_->now(nowMs);
    const tm local = getLocalTime();

    Serial.printf("[TIME] UTC %04d-%02d-%02d %02d:%02d:%02d | LOCAL %04d-%02d-%02d %02d:%02d:%02d | RTC:%s | NTP:%s\n",
                  utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second,
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec,
                  rtcStr, ntpStr);
}