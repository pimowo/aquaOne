#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Web {
namespace Internal {

// RFC 6455 opcodes; keep control handling separate from product JSON messages.
constexpr uint8_t WS_CLOSE = 0x8U;
constexpr uint8_t WS_PING = 0x9U;
constexpr uint8_t WS_PONG = 0xAU;
constexpr size_t WS_CONTROL_MAX_LENGTH = 125U;

enum class WebSocketControlResult { Handled, Unsupported, Malformed, SendFailed };
typedef bool (*SendWebSocketControl)(void*, uint8_t, const uint8_t*, size_t);

inline bool isWebSocketControl(uint8_t opcode) {
    return opcode == WS_CLOSE || opcode == WS_PING || opcode == WS_PONG;
}

inline bool validCloseReason(const uint8_t* text, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const uint8_t lead = text[offset++];
        if (lead < 0x80U) continue;
        size_t trailing = 0U;
        uint8_t firstMin = 0x80U;
        uint8_t firstMax = 0xBFU;
        if (lead >= 0xC2U && lead <= 0xDFU) trailing = 1U;
        else if (lead >= 0xE0U && lead <= 0xEFU) {
            trailing = 2U;
            if (lead == 0xE0U) firstMin = 0xA0U;
            if (lead == 0xEDU) firstMax = 0x9FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            trailing = 3U;
            if (lead == 0xF0U) firstMin = 0x90U;
            if (lead == 0xF4U) firstMax = 0x8FU;
        } else return false;
        if (length - offset < trailing || text[offset] < firstMin ||
            text[offset] > firstMax) return false;
        ++offset;
        for (size_t i = 1U; i < trailing; ++i) {
            if (text[offset] < 0x80U || text[offset] > 0xBFU) return false;
            ++offset;
        }
    }
    return true;
}

inline bool validClosePayload(const uint8_t* payload, size_t length) {
    if (length == 0U) return true;
    if (length == 1U) return false;
    const uint16_t code = static_cast<uint16_t>(
        (static_cast<uint16_t>(payload[0]) << 8U) | payload[1]);
    const bool standard = code >= 1000U && code <= 1014U &&
        code != 1004U && code != 1005U && code != 1006U;
    if (!standard && !(code >= 3000U && code <= 4999U)) return false;
    return validCloseReason(payload + 2U, length - 2U);
}

// send must complete synchronously: reply borrows the received payload.
// Session teardown is owned by HTTPD, then existing inactive-fd reclamation.
inline WebSocketControlResult handleWebSocketControl(
    uint8_t opcode, bool final, const uint8_t* payload, size_t length,
    SendWebSocketControl send, void* context
) {
    if (!isWebSocketControl(opcode))
        return WebSocketControlResult::Unsupported;
    if (!final || length > WS_CONTROL_MAX_LENGTH ||
        (length != 0U && payload == nullptr) ||
        (opcode == WS_CLOSE && !validClosePayload(payload, length)))
        return WebSocketControlResult::Malformed;
    if (opcode == WS_PONG) return WebSocketControlResult::Handled;
    const uint8_t reply = opcode == WS_PING ? WS_PONG : WS_CLOSE;
    return send != nullptr && send(context, reply, payload, length)
        ? WebSocketControlResult::Handled : WebSocketControlResult::SendFailed;
}

} // namespace Internal
} // namespace Web
} // namespace AquaCore
