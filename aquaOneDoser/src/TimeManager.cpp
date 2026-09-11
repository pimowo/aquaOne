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

TimeManager::TimeManager() = default;

TimeManager::~TimeManager() {
    delete ntpService_;
    delete ntpBackend_;
    delete resilientTime_;
    delete rtc_;
    delete bus_;
}

bool TimeManager::begin() {
    Wire.setTimeOut(100);

    bus_ = new DoserRtcBus();

    AquaCore::Time::RtcConfig rtcCfg {};
    rtcCfg.sdaPin = PIN_I2C_SDA;
    rtcCfg.sclPin = PIN_I2C_SCL;
    rtcCfg.i2cAddress = 0x68U;

    rtc_ = new AquaCore::Time::RtcService(*bus_, rtcCfg);

    AquaCore::Time::ResilientTimeConfig resilientCfg {};
    resilientCfg.failureThreshold = RTC_FAILURE_THRESHOLD;
    resilientCfg.recoveryThreshold = RTC_RECOVERY_THRESHOLD;

    resilientTime_ = new AquaCore::Time::ResilientTimeService(*rtc_, resilientCfg);

    const uint32_t nowMs = millis();
    const bool rtcOk = resilientTime_->begin(nowMs);

    if (rtcOk) {
        Serial.println("[RTC] DS3231 OK");
    } else {
        Serial.println("[RTC] DS3231 nie znaleziony lub odczyt nieudany");
    }

    ntpBackend_ = new AquaCore::Time::EspNtpBackend();
    ntpService_ = new AquaCore::Time::NtpService(*rtc_, *ntpBackend_);
    ntpService_->begin(nowMs);

    lastRtcPoll_ = nowMs;
    return rtcOk;
}

void TimeManager::loop() {
    const unsigned long nowMs = millis();

    if (nowMs - lastRtcPoll_ >= RTC_READ_INTERVAL_MS) {
        lastRtcPoll_ = nowMs;
        if (resilientTime_ != nullptr) {
            resilientTime_->poll(nowMs);
        }
    }

    const bool wifiAvailable = (WiFi.status() == WL_CONNECTED);

    if (ntpService_ != nullptr) {
        ntpService_->update(wifiAvailable, nowMs);

        if (wifiAvailable) {
            ntpService_->requestPeriodicSync(true, nowMs);
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
            Serial.println("[NTP] Synchronizacja OK");
        }
    }
}

bool TimeManager::isRtcOk() const {
    return resilientTime_ != nullptr && resilientTime_->isRtcHealthy();
}

bool TimeManager::isTimeValid() const {
    return resilientTime_ != nullptr && resilientTime_->isValid();
}

bool TimeManager::isNtpSynced() const {
    return ntpSynced_;
}

uint32_t TimeManager::getLastNtpSyncTimestamp() const {
    return lastNtpSyncTimestamp_;
}

uint32_t TimeManager::getUtcTimestamp() const {
    if (resilientTime_ == nullptr || !resilientTime_->isValid()) {
        return 0U;
    }

    const AquaCore::Time::LocalTime utc = resilientTime_->now(millis());
    const uint32_t stamp = localTimeToEpoch(utc);
    return stamp >= MIN_VALID_TIMESTAMP ? stamp : 0U;
}

tm TimeManager::getLocalTime() const {
    tm result {};
    if (resilientTime_ == nullptr || !resilientTime_->isValid()) {
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
    if (!isTimeValid()) {
        Serial.println("[TIME] RTC ERROR");
        return;
    }

    const uint32_t nowMs = millis();
    const AquaCore::Time::LocalTime utc = resilientTime_->now(nowMs);
    const tm local = getLocalTime();

    Serial.printf("[TIME] UTC %04d-%02d-%02d %02d:%02d:%02d | LOCAL %04d-%02d-%02d %02d:%02d:%02d | RTC:%s | NTP:%s\n",
                  utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second,
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec,
                  isRtcOk() ? "OK" : "ERROR",
                  isNtpSynced() ? "OK" : "BRAK");
}