#include "HydroWebProtocol.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{

constexpr const char* SETTINGS_FIELDS[] = {
    "floatActiveLow", "floatUsePullup", "floatDebounceMs",
    "ultrasonicMinDistanceCm", "ultrasonicMaxDistanceCm",
    "ultrasonicTimeoutUs", "tankEmptyDistanceCm", "tankFullDistanceCm",
    "tankSampleIntervalMs", "tankMaxFailedSeries", "reserveLowPercent",
    "reserveCriticalPercent", "reserveHysteresisPercent",
    "topupStartDelayMs", "topupMaxPumpRuntimeMs", "staEnabled", "ssid",
    "password", "hostname", "autoReconnect", "reconnectMs", "apEnabled",
    "apSsid", "apPassword"
};

constexpr size_t SETTINGS_FIELD_COUNT =
    sizeof(SETTINGS_FIELDS) / sizeof(SETTINGS_FIELDS[0]);

static_assert(SETTINGS_FIELD_COUNT == 24U,
              "Hydro settings wire contract must contain 24 fields");

constexpr uint32_t OPTIONAL_SETTINGS_FIELDS =
    (1UL << 0U) | (1UL << 1U) | (1UL << 15U) | (1UL << 17U) |
    (1UL << 19U) | (1UL << 21U) | (1UL << 23U);

int hexValue(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool validEncoding(const char* body, size_t length)
{
    if (body == nullptr || length == 0U) return false;
    for (size_t i = 0U; i < length; ++i)
    {
        if (body[i] == '\0') return false;
        if (body[i] == '%')
        {
            if (i + 2U >= length)
            {
                return false;
            }
            const int high = hexValue(body[i + 1U]);
            const int low = hexValue(body[i + 2U]);
            if (high < 0 || low < 0 || ((high << 4) | low) == 0) return false;
            i += 2U;
        }
    }
    return true;
}

int fieldIndex(const char* key, size_t keyLength,
               const char* const* fields, size_t fieldCount)
{
    for (size_t index = 0U; index < fieldCount; ++index)
    {
        const size_t expectedLength = std::strlen(fields[index]);
        if (keyLength == expectedLength &&
            std::memcmp(key, fields[index], keyLength) == 0)
            return static_cast<int>(index);
    }
    return -1;
}

bool validateFieldSet(const char* body, size_t length,
                      const char* const* fields, size_t fieldCount,
                      uint32_t optionalMask)
{
    if (body == nullptr || length == 0U || fieldCount > 32U) return false;
    uint32_t seen = 0U;
    size_t start = 0U;
    while (start < length)
    {
        size_t end = start;
        while (end < length && body[end] != '&') ++end;
        if (end == start) return false;
        size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end) return false;
        const int index = fieldIndex(
            body + start, equals - start, fields, fieldCount);
        if (index < 0) return false;
        const uint32_t bit = 1UL << static_cast<uint32_t>(index);
        if ((seen & bit) != 0U) return false;
        seen |= bit;
        if (end == length) break;
        start = end + 1U;
        if (start == length) return false;
    }
    const uint32_t allFields = fieldCount == 32U
        ? 0xFFFFFFFFUL : ((1UL << fieldCount) - 1UL);
    return (seen | optionalMask) == allFields;
}

bool decode(const char* input, size_t length, char* output, size_t capacity)
{
    if (input == nullptr || output == nullptr || capacity == 0U) return false;
    size_t written = 0U;
    for (size_t i = 0U; i < length; ++i)
    {
        char value = input[i];
        if (value == '+') value = ' ';
        else if (value == '%')
        {
            if (i + 2U >= length) return false;
            const int high = hexValue(input[i + 1U]);
            const int low = hexValue(input[i + 2U]);
            if (high < 0 || low < 0) return false;
            value = static_cast<char>((high << 4) | low);
            i += 2U;
            if (value == '\0') return false;
        }
        if (written + 1U >= capacity) return false;
        output[written++] = value;
    }
    output[written] = '\0';
    return true;
}

bool findValue(const char* body, size_t length, const char* key,
               const char*& value, size_t& valueLength)
{
    if (body == nullptr || key == nullptr) return false;
    const size_t keyLength = std::strlen(key);
    size_t start = 0U;
    while (start < length)
    {
        size_t end = start;
        while (end < length && body[end] != '&') ++end;
        size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals < end && equals - start == keyLength &&
            std::memcmp(body + start, key, keyLength) == 0)
        {
            value = body + equals + 1U;
            valueLength = end - equals - 1U;
            return true;
        }
        start = end + 1U;
    }
    return false;
}

bool copyValue(const char* body, size_t length, const char* key,
               char* output, size_t capacity, bool required = true)
{
    const char* value = nullptr;
    size_t valueLength = 0U;
    if (!findValue(body, length, key, value, valueLength))
    {
        if (output != nullptr && capacity != 0U) output[0] = '\0';
        return !required;
    }
    return decode(value, valueLength, output, capacity);
}

bool hasKey(const char* body, size_t length, const char* key)
{
    const char* ignored = nullptr;
    size_t ignoredLength = 0U;
    return findValue(body, length, key, ignored, ignoredLength);
}

bool parseUnsigned(const char* body, size_t length, const char* key,
                   uint32_t& output)
{
    char text[16] {};
    if (!copyValue(body, length, key, text, sizeof(text)) || text[0] == '\0')
        return false;
    for (size_t index = 0U; text[index] != '\0'; ++index)
        if (text[index] < '0' || text[index] > '9') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value > std::numeric_limits<uint32_t>::max()) return false;
    output = static_cast<uint32_t>(value);
    return true;
}

bool parseByte(const char* body, size_t length, const char* key, uint8_t& output)
{
    uint32_t value = 0U;
    if (!parseUnsigned(body, length, key, value) || value > 255U) return false;
    output = static_cast<uint8_t>(value);
    return true;
}

bool parseFloat(const char* body, size_t length, const char* key, float& output)
{
    // E1 accepted at most 16 decoded lexical bytes for every float.
    char text[17] {};
    if (!copyValue(body, length, key, text, sizeof(text)) || text[0] == '\0')
        return false;
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !std::isfinite(value))
        return false;
    output = value;
    return true;
}

}

bool parseHydroControlRequest(
    const char* body, size_t bodyLength, HydroWebRequest& request
)
{
    if (!validEncoding(body, bodyLength)) return false;
    static const char* const fields[] = {"action"};
    if (!validateFieldSet(body, bodyLength, fields, 1U, 0U)) return false;
    char action[32] {};
    if (!copyValue(body, bodyLength, "action", action, sizeof(action)))
        return false;
    if (std::strcmp(action, "service_toggle") == 0)
        request.kind = HydroWebRequestKind::ToggleServiceMode;
    else if (std::strcmp(action, "mute") == 0)
        request.kind = HydroWebRequestKind::MuteBuzzer;
    else if (std::strcmp(action, "reset_lockout") == 0)
        request.kind = HydroWebRequestKind::ResetLockout;
    else return false;
    return true;
}

bool parseHydroSettingsRequest(
    const char* body, size_t bodyLength, HydroSettingsRequest& output
)
{
    if (!validEncoding(body, bodyLength)) return false;
    if (!validateFieldSet(body, bodyLength, SETTINGS_FIELDS,
                          SETTINGS_FIELD_COUNT, OPTIONAL_SETTINGS_FIELDS))
        return false;
    HydroSettingsRequest value {};
    value.floatActiveLow = hasKey(body, bodyLength, "floatActiveLow");
    value.floatUsePullup = hasKey(body, bodyLength, "floatUsePullup");
    value.wifiStaEnabled = hasKey(body, bodyLength, "staEnabled");
    value.wifiAutoReconnect = hasKey(body, bodyLength, "autoReconnect");
    value.wifiApEnabled = hasKey(body, bodyLength, "apEnabled");

    if (!parseUnsigned(body, bodyLength, "floatDebounceMs", value.floatDebounceMs) ||
        !parseFloat(body, bodyLength, "ultrasonicMinDistanceCm", value.ultrasonicMinDistanceCm) ||
        !parseFloat(body, bodyLength, "ultrasonicMaxDistanceCm", value.ultrasonicMaxDistanceCm) ||
        !parseUnsigned(body, bodyLength, "ultrasonicTimeoutUs", value.ultrasonicTimeoutUs) ||
        !parseFloat(body, bodyLength, "tankEmptyDistanceCm", value.tankEmptyDistanceCm) ||
        !parseFloat(body, bodyLength, "tankFullDistanceCm", value.tankFullDistanceCm) ||
        !parseUnsigned(body, bodyLength, "tankSampleIntervalMs", value.tankSampleIntervalMs) ||
        !parseByte(body, bodyLength, "tankMaxFailedSeries", value.tankMaxFailedSeries) ||
        !parseFloat(body, bodyLength, "reserveLowPercent", value.reserveLowPercent) ||
        !parseFloat(body, bodyLength, "reserveCriticalPercent", value.reserveCriticalPercent) ||
        !parseFloat(body, bodyLength, "reserveHysteresisPercent", value.reserveHysteresisPercent) ||
        !parseUnsigned(body, bodyLength, "topupStartDelayMs", value.topupStartDelayMs) ||
        !parseUnsigned(body, bodyLength, "topupMaxPumpRuntimeMs", value.topupMaxPumpRuntimeMs) ||
        !copyValue(body, bodyLength, "ssid", value.wifiSsid, sizeof(value.wifiSsid)) ||
        !copyValue(body, bodyLength, "hostname", value.wifiHostname, sizeof(value.wifiHostname)) ||
        !parseUnsigned(body, bodyLength, "reconnectMs", value.wifiReconnectIntervalMs) ||
        !copyValue(body, bodyLength, "apSsid", value.wifiApSsid, sizeof(value.wifiApSsid)))
    {
        return false;
    }

    if (!copyValue(body, bodyLength, "password", value.wifiPassword,
                   sizeof(value.wifiPassword), false) ||
        !copyValue(body, bodyLength, "apPassword", value.wifiApPassword,
                   sizeof(value.wifiApPassword), false))
    {
        return false;
    }
    value.replaceWifiPassword = value.wifiPassword[0] != '\0';
    value.replaceApPassword = value.wifiApPassword[0] != '\0';
    output = value;
    return true;
}
