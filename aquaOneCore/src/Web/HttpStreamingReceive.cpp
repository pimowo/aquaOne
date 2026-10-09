#include "HttpStreamingReceive.h"

namespace AquaCore {
namespace Web {
namespace Internal {

StreamReceiveResult receiveStream(
    const HttpStreamRequest& request, size_t maxContentLength,
    HttpStreamHandler handler, void* handlerContext, WebResponseWriter& response,
    uint8_t* buffer, StreamRead reader, void* readerContext,
    StreamAccepting accepting, void* acceptingContext, int timeoutCode,
    StreamOversize oversize, void* oversizeContext
) {
    if (request.contentLength > maxContentLength) {
        if (oversize != nullptr) oversize(oversizeContext);
        return StreamReceiveResult::Oversize;
    }
    if (handler == nullptr || buffer == nullptr || reader == nullptr ||
        accepting == nullptr) return StreamReceiveResult::Invalid;

    const auto emitAbort = [&](HttpStreamAbortReason reason) {
        HttpStreamEvent event {HttpStreamEventType::BodyAbort};
        event.abortReason = reason;
        (void)handler(handlerContext, event, response);
    };
    HttpStreamEvent start {HttpStreamEventType::BodyStart};
    start.request = &request;
    if (handler(handlerContext, start, response) == HttpStreamHandlerResult::Stop) {
        emitAbort(HttpStreamAbortReason::HandlerStopped);
        return request.contentLength == 0U ? StreamReceiveResult::StoppedConsumed
                                           : StreamReceiveResult::StoppedUnread;
    }

    size_t remaining = request.contentLength;
    while (remaining != 0U) {
        if (!accepting(acceptingContext)) {
            emitAbort(HttpStreamAbortReason::TransportStopping);
            return StreamReceiveResult::Aborted;
        }
        const size_t requested = remaining < HTTP_STREAM_CHUNK_CAPACITY
            ? remaining : HTTP_STREAM_CHUNK_CAPACITY;
        const int received = reader(readerContext, buffer, requested);
        if (!accepting(acceptingContext)) {
            emitAbort(HttpStreamAbortReason::TransportStopping);
            return StreamReceiveResult::Aborted;
        }
        if (received <= 0 || static_cast<size_t>(received) > requested) {
            const HttpStreamAbortReason reason = received == 0
                ? HttpStreamAbortReason::PeerClosed
                : received == timeoutCode
                    ? HttpStreamAbortReason::ReceiveTimeout
                    : HttpStreamAbortReason::ReceiveError;
            emitAbort(reason);
            return StreamReceiveResult::Aborted;
        }
        remaining -= static_cast<size_t>(received);
        HttpStreamEvent data {HttpStreamEventType::BodyData};
        data.data = buffer;
        data.length = static_cast<size_t>(received);
        if (handler(handlerContext, data, response) == HttpStreamHandlerResult::Stop) {
            emitAbort(HttpStreamAbortReason::HandlerStopped);
            return remaining == 0U ? StreamReceiveResult::StoppedConsumed
                                    : StreamReceiveResult::StoppedUnread;
        }
    }
    if (!accepting(acceptingContext)) {
        emitAbort(HttpStreamAbortReason::TransportStopping);
        return StreamReceiveResult::Aborted;
    }
    HttpStreamEvent end {HttpStreamEventType::BodyEnd};
    if (handler(handlerContext, end, response) == HttpStreamHandlerResult::Stop) {
        emitAbort(HttpStreamAbortReason::HandlerStopped);
        return StreamReceiveResult::StoppedConsumed;
    }
    return StreamReceiveResult::Complete;
}

} // namespace Internal
} // namespace Web
} // namespace AquaCore
