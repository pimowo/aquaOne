#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/Web/RealtimeResync.h"

namespace AquaCore {
namespace Web {

// The longest valid frame is At(UINT64_MAX), 114 wire bytes. Storage includes
// one trailing NUL for callers that inspect the JSON as text; it is not sent.
constexpr size_t STREAM_START_WIRE_MAX_LENGTH = 114U;
constexpr size_t STREAM_START_WIRE_CAPACITY = 128U;

// Failure sets length to zero and leaves caller storage unchanged. No heap or
// transport dependency is involved; the returned length excludes the NUL.
inline bool encodeRealtimeStreamStart(
    const RealtimeStreamStartState& state,
    uint8_t* output, size_t capacity, size_t& length
) {
    length = 0U;
    if (!state.isAvailable() || output == nullptr) return false;

    char runtime[Identity::RuntimeIdentity::TEXT_CAPACITY] {};
    if (!state.runtimeIdentity().format(runtime, sizeof(runtime))) return false;

    char digits[20U] {};
    size_t digitCount = 0U;
    const RealtimeStreamPosition position = state.position();
    if (!position.isBeforeFirst()) {
        if (!position.hasSequence()) return false;
        uint64_t sequence = position.sequence().value();
        if (sequence == 0U) return false;
        do {
            digits[digitCount++] = static_cast<char>('0' + (sequence % 10U));
            sequence /= 10U;
        } while (sequence != 0U);
    }

    static const char prefix[] = "{\"type\":\"stream_start\",\"runtime_id\":\"";
    static const char beforeFirst[] = "\",\"position\":{\"kind\":\"before_first\"}}";
    static const char atPrefix[] = "\",\"position\":{\"kind\":\"at\",\"sequence\":\"";
    static const char atSuffix[] = "\"}}";
    const size_t wireLength = sizeof(prefix) - 1U + 16U +
        (position.isBeforeFirst() ? sizeof(beforeFirst) - 1U :
         sizeof(atPrefix) - 1U + digitCount + sizeof(atSuffix) - 1U);
    if (wireLength > STREAM_START_WIRE_MAX_LENGTH ||
        capacity <= wireLength) return false;

    char frame[STREAM_START_WIRE_CAPACITY] {};
    size_t cursor = 0U;
    memcpy(frame + cursor, prefix, sizeof(prefix) - 1U);
    cursor += sizeof(prefix) - 1U;
    memcpy(frame + cursor, runtime, 16U);
    cursor += 16U;
    if (position.isBeforeFirst()) {
        memcpy(frame + cursor, beforeFirst, sizeof(beforeFirst) - 1U);
        cursor += sizeof(beforeFirst) - 1U;
    } else {
        memcpy(frame + cursor, atPrefix, sizeof(atPrefix) - 1U);
        cursor += sizeof(atPrefix) - 1U;
        while (digitCount != 0U) frame[cursor++] = digits[--digitCount];
        memcpy(frame + cursor, atSuffix, sizeof(atSuffix) - 1U);
        cursor += sizeof(atSuffix) - 1U;
    }
    frame[cursor] = '\0';
    memcpy(output, frame, cursor + 1U);
    length = cursor;
    return true;
}

} // namespace Web
} // namespace AquaCore
