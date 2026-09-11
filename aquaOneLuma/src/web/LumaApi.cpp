#include "LumaApi.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "AquaCore/Web/WebTypes.h"
#include "../../include/Constants.h"

namespace LumaSense {
namespace Web {
namespace {

using AquaCore::Web::ContentType;
using AquaCore::Web::HttpMethod;
using AquaCore::Web::WebRequest;
using AquaCore::Web::WebResponseWriter;
using AquaCore::Web::writeJsonString;

constexpr size_t MAX_CONTROL_BODY = 512U;

class JsonCursor {
public:
    JsonCursor(const char* data, size_t length)
        : current_(data), end_(data != nullptr ? data + length : nullptr) {}

    bool beginObject() { return consume('{'); }
    bool endObject() {
        if (!consume('}')) return false;
        skipWhitespace();
        return current_ == end_;
    }
    bool comma() { return consume(','); }
    bool key(const char* expected) {
        skipWhitespace();
        if (!raw('"')) return false;
        while (*expected != '\0') {
            if (current_ == end_ || *current_ != *expected) return false;
            ++current_; ++expected;
        }
        return raw('"') && consume(':');
    }
    bool text(char* output, size_t capacity) {
        if (!output || capacity == 0U) return false;
        skipWhitespace();
        if (!raw('"')) return false;
        size_t length = 0U;
        while (current_ != end_ && *current_ != '"') {
            const unsigned char value = static_cast<unsigned char>(*current_);
            if (*current_ == '\\' || value < 0x20U || length + 1U >= capacity)
                return false;
            output[length++] = *current_++;
        }
        if (!raw('"')) return false;
        output[length] = '\0';
        return true;
    }
    bool unsignedInteger(uint16_t& output) {
        skipWhitespace();
        if (current_ == end_ || *current_ < '0' || *current_ > '9') return false;
        uint32_t value = 0U;
        while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
            value = value * 10U + static_cast<uint32_t>(*current_ - '0');
            if (value > UINT16_MAX) return false;
            ++current_;
        }
        output = static_cast<uint16_t>(value);
        return true;
    }
    bool number(float& output) {
        skipWhitespace();
        if (current_ == end_) return false;
        bool negative = false;
        if (*current_ == '-') { negative = true; ++current_; }
        if (current_ == end_ || *current_ < '0' || *current_ > '9') return false;
        double value = 0.0;
        while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
            value = value * 10.0 + static_cast<double>(*current_ - '0');
            ++current_;
        }
        if (current_ != end_ && *current_ == '.') {
            ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') return false;
            double factor = 0.1;
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
                value += static_cast<double>(*current_ - '0') * factor;
                factor *= 0.1;
                ++current_;
            }
        }
        output = static_cast<float>(negative ? -value : value);
        return std::isfinite(output);
    }
    bool beginArray() { return consume('['); }
    bool endArray() { return consume(']'); }

private:
    void skipWhitespace() {
        while (current_ && current_ != end_ &&
               (*current_ == ' ' || *current_ == '\t' ||
                *current_ == '\r' || *current_ == '\n')) ++current_;
    }
    bool consume(char value) { skipWhitespace(); return raw(value); }
    bool raw(char value) {
        if (!current_ || current_ == end_ || *current_ != value) return false;
        ++current_; return true;
    }
    const char* current_;
    const char* end_;
};

bool validBody(const WebRequest& request) {
    return request.method == HttpMethod::Post && request.body &&
           request.bodyLength > 0U && request.bodyLength <= MAX_CONTROL_BODY;
}

bool parseMode(const WebRequest& request, char mode[16]) {
    if (!validBody(request)) return false;
    JsonCursor cursor(request.body, request.bodyLength);
    return cursor.beginObject() && cursor.key("mode") &&
           cursor.text(mode, 16U) && cursor.endObject();
}

bool parseProfile(const WebRequest& request, uint8_t& profileIndex) {
    if (!validBody(request)) return false;
    uint16_t profile = 0U;
    JsonCursor cursor(request.body, request.bodyLength);
    if (!cursor.beginObject() || !cursor.key("profile") ||
        !cursor.unsignedInteger(profile) || !cursor.endObject() ||
        profile < 1U || profile > PROFILE_COUNT) return false;
    profileIndex = static_cast<uint8_t>(profile - 1U);
    return true;
}

bool parseManual(const WebRequest& request, ChannelLevels& levels,
                 uint16_t& timeoutMinutes) {
    if (!validBody(request)) return false;
    JsonCursor cursor(request.body, request.bodyLength);
    if (!cursor.beginObject() || !cursor.key("levels") || !cursor.beginArray())
        return false;
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (!cursor.number(levels.value[channel]) ||
            levels.value[channel] < LEVEL_MIN_PERCENT ||
            levels.value[channel] > LEVEL_MAX_PERCENT) return false;
        if (channel + 1U < CHANNEL_COUNT && !cursor.comma()) return false;
    }
    return cursor.endArray() && cursor.comma() &&
           cursor.key("timeoutMinutes") &&
           cursor.unsignedInteger(timeoutMinutes) &&
           (timeoutMinutes == 0U || timeoutMinutes == 15U ||
            timeoutMinutes == 30U || timeoutMinutes == 60U) &&
           cursor.endObject();
}

const char* modeName(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Normal: return "NORMAL";
        case OperatingMode::Service: return "SERVICE";
        case OperatingMode::Manual: return "MANUAL";
        case OperatingMode::ChannelTest: return "CHANNEL_TEST";
        case OperatingMode::Preview: return "PREVIEW";
        case OperatingMode::Simulation: return "SIMULATION";
        case OperatingMode::Off: return "OFF";
    }
    return "UNKNOWN";
}
const char* dayStateName(DayState state) {
    return state == DayState::Night ? "NIGHT" : "DAY";
}
const char* networkStateName(AquaCore::Network::NetworkState state) {
    using AquaCore::Network::NetworkState;
    switch (state) {
        case NetworkState::Disabled: return "disabled";
        case NetworkState::Idle: return "idle";
        case NetworkState::Connecting: return "connecting";
        case NetworkState::Connected: return "connected";
        case NetworkState::Disconnected: return "disconnected";
        case NetworkState::Error: return "error";
    }
    return "unknown";
}
const char* healthName(AquaCore::Diagnostics::HealthState state) {
    using AquaCore::Diagnostics::HealthState;
    switch (state) {
        case HealthState::Ok: return "ok";
        case HealthState::Unknown: return "unknown";
        case HealthState::Warning: return "warning";
        case HealthState::Error: return "error";
    }
    return "unknown";
}

bool writeUnsigned(WebResponseWriter& response, uint32_t value) {
    char text[16] {};
    const int length = std::snprintf(text, sizeof(text), "%lu",
                                     static_cast<unsigned long>(value));
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
           response.write(text, static_cast<size_t>(length));
}
bool writeSigned(WebResponseWriter& response, int32_t value) {
    char text[16] {};
    const int length = std::snprintf(text, sizeof(text), "%ld",
                                     static_cast<long>(value));
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
           response.write(text, static_cast<size_t>(length));
}
bool writeFloat(WebResponseWriter& response, float value) {
    if (!std::isfinite(value)) value = 0.0f;
    char text[20] {};
    const int length = std::snprintf(text, sizeof(text), "%.2f",
                                     static_cast<double>(value));
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
           response.write(text, static_cast<size_t>(length));
}
bool writeIp(WebResponseWriter& response,
             const AquaCore::Network::IpAddress& address) {
    char text[16] {};
    const int length = std::snprintf(
        text, sizeof(text), "%u.%u.%u.%u",
        static_cast<unsigned int>(address.octets[0]),
        static_cast<unsigned int>(address.octets[1]),
        static_cast<unsigned int>(address.octets[2]),
        static_cast<unsigned int>(address.octets[3]));
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
           writeJsonString(response, text);
}
bool writeLocalTime(WebResponseWriter& response, const LocalTime& value) {
    if (!value.valid) return writeJsonString(response, "invalid");
    char text[24] {};
    const int length = std::snprintf(
        text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned int>(value.year),
        static_cast<unsigned int>(value.month),
        static_cast<unsigned int>(value.day),
        static_cast<unsigned int>(value.hour),
        static_cast<unsigned int>(value.minute),
        static_cast<unsigned int>(value.second));
    return length > 0 && static_cast<size_t>(length) < sizeof(text) &&
           writeJsonString(response, text);
}

void errorResponse(WebResponseWriter& response, uint16_t status,
                   const char* message) {
    response.beginResponse(status, ContentType::Json);
    response.writeText("{\"ok\":false,\"error\":");
    writeJsonString(response, message);
    response.writeText("}");
    response.endResponse();
}
void commandResponse(WebResponseWriter& response,
                     FirmwareCommandResult result) {
    uint16_t status = 200U;
    const char* name = "applied";
    bool ok = true;
    switch (result) {
        case FirmwareCommandResult::Applied: break;
        case FirmwareCommandResult::NoChange: name = "no_change"; break;
        case FirmwareCommandResult::Invalid:
            errorResponse(response, 400U, "invalid command"); return;
        case FirmwareCommandResult::Rejected:
            status = 409U; name = "state conflict"; ok = false; break;
        case FirmwareCommandResult::StorageFailure:
            status = 409U; name = "storage unavailable"; ok = false; break;
    }
    response.beginResponse(status, ContentType::Json);
    response.writeText(ok ? "{\"ok\":true,\"result\":" :
                            "{\"ok\":false,\"error\":");
    writeJsonString(response, name);
    response.writeText("}");
    response.endResponse();
}

} // namespace

StatusApi::StatusApi(const FirmwareApp& app,
                     const AquaCore::Network::NetworkService& network,
                     const AquaCore::Diagnostics::DiagnosticsService& diagnostics)
    : app_(app), network_(network), diagnostics_(diagnostics) {}
const char* StatusApi::route() const { return "/api/lumasense/status"; }
HttpMethod StatusApi::method() const { return HttpMethod::Get; }
void StatusApi::handle(const WebRequest& request, WebResponseWriter& response) {
    if (request.method != HttpMethod::Get) {
        errorResponse(response, 400U, "invalid method"); return;
    }
    const RuntimeState& state = app_.state();
    const DeviceConfig& config = app_.config();
    const auto diagnostics = diagnostics_.snapshot();
    response.beginResponse(200U, ContentType::Json);
    response.writeText("{\"mode\":"); writeJsonString(response, modeName(state.mode));
    response.writeText(",\"activeProfile\":");
    writeUnsigned(response, static_cast<uint32_t>(config.activeProfileIndex) + 1U);
    response.writeText(",\"activeProfileName\":");
    writeJsonString(response, config.profiles[config.activeProfileIndex].name);
    response.writeText(",\"dayState\":"); writeJsonString(response, dayStateName(state.dayState));
    response.writeText(",\"localTime\":"); writeLocalTime(response, app_.localTime());
    response.writeText(",\"requestedLevels\":[");
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (channel) response.writeText(",");
        writeFloat(response, state.requestedLevels.value[channel]);
    }
    response.writeText("],\"finalLevels\":[");
    for (uint8_t channel = 0U; channel < CHANNEL_COUNT; ++channel) {
        if (channel) response.writeText(",");
        writeFloat(response, state.actualLevels.value[channel]);
    }
    response.writeText("],\"globalPowerLimit\":");
    writeFloat(response, config.globalPowerLimitPercent);
    response.writeText(",\"timeValid\":");
    response.writeText(state.timeValid ? "true" : "false");
    response.writeText(",\"wifi\":{\"state\":");
    writeJsonString(response, networkStateName(network_.state()));
    response.writeText(",\"connected\":");
    response.writeText(network_.isConnected() ? "true" : "false");
    response.writeText(",\"ip\":"); writeIp(response, network_.ipAddress());
    response.writeText(",\"rssi\":"); writeSigned(response, network_.rssi());
    response.writeText("},\"overallHealth\":");
    writeJsonString(response, healthName(diagnostics.overallHealth));
    response.writeText("}");
    response.endResponse();
}

ModeApi::ModeApi(FirmwareApp& app) : app_(app) {}
const char* ModeApi::route() const { return "/api/lumasense/mode"; }
HttpMethod ModeApi::method() const { return HttpMethod::Post; }
void ModeApi::handle(const WebRequest& request, WebResponseWriter& response) {
    char requested[16] {};
    if (!parseMode(request, requested)) {
        errorResponse(response, 400U, "malformed request"); return;
    }
    if (std::strcmp(requested, "EXIT_MANUAL") == 0) {
        commandResponse(response, app_.exitManual()); return;
    }
    OperatingMode mode;
    if (std::strcmp(requested, "NORMAL") == 0) mode = OperatingMode::Normal;
    else if (std::strcmp(requested, "SERVICE") == 0) mode = OperatingMode::Service;
    else if (std::strcmp(requested, "OFF") == 0) mode = OperatingMode::Off;
    else { errorResponse(response, 400U, "invalid mode"); return; }
    commandResponse(response, app_.commandMode(mode));
}

ProfileApi::ProfileApi(FirmwareApp& app) : app_(app) {}
const char* ProfileApi::route() const { return "/api/lumasense/profile"; }
HttpMethod ProfileApi::method() const { return HttpMethod::Post; }
void ProfileApi::handle(const WebRequest& request, WebResponseWriter& response) {
    uint8_t profileIndex = 0U;
    if (!parseProfile(request, profileIndex)) {
        errorResponse(response, 400U, "invalid profile"); return;
    }
    commandResponse(response, app_.setActiveProfileIndex(profileIndex));
}

ManualApi::ManualApi(FirmwareApp& app, const uint32_t& nowMs)
    : app_(app), nowMs_(nowMs) {}
const char* ManualApi::route() const { return "/api/lumasense/manual"; }
HttpMethod ManualApi::method() const { return HttpMethod::Post; }
void ManualApi::handle(const WebRequest& request, WebResponseWriter& response) {
    ChannelLevels levels {};
    uint16_t timeoutMinutes = 0U;
    if (!parseManual(request, levels, timeoutMinutes)) {
        errorResponse(response, 400U, "invalid manual levels"); return;
    }
    commandResponse(response,
        app_.commandManual(levels, timeoutMinutes, nowMs_));
}

} // namespace Web
} // namespace LumaSense
