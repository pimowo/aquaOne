#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/Web/HttpStreamingServerTransport.h"

namespace AquaCore {
namespace Web {
namespace Internal {

constexpr size_t HTTP_STREAM_CHUNK_CAPACITY = 1024U;

using StreamRead = int (*)(void* context, uint8_t* buffer, size_t capacity);
using StreamAccepting = bool (*)(void* context);
using StreamOversize = void (*)(void* context);

enum class StreamReceiveResult : uint8_t {
    Complete, StoppedConsumed, StoppedUnread, Aborted, Oversize, Invalid
};

// The caller owns buffer storage and the response writer. The reader must
// return at most capacity bytes, zero for close, or a negative error code.
StreamReceiveResult receiveStream(
    const HttpStreamRequest& request, size_t maxContentLength,
    HttpStreamHandler handler, void* handlerContext, WebResponseWriter& response,
    uint8_t* buffer, StreamRead reader, void* readerContext,
    StreamAccepting accepting, void* acceptingContext, int timeoutCode,
    StreamOversize oversize, void* oversizeContext
);

} // namespace Internal
} // namespace Web
} // namespace AquaCore
