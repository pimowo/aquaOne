#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/Web/WebTypes.h"

namespace AquaCore {
namespace Web {

enum class HttpStreamEventType : uint8_t {
    BodyStart, BodyData, BodyEnd, BodyAbort
};

enum class HttpStreamAbortReason : uint8_t {
    PeerClosed, ReceiveTimeout, ReceiveError, HandlerStopped, TransportStopping
};

enum class HttpStreamHandlerResult : uint8_t { Continue, Stop };

struct HttpStreamRequest {
    HttpMethod method;
    const char* path;
    size_t contentLength;
    // Available only during BodyStart. Copy headers before sending a response:
    // the server may release them when response transmission begins.
    const WebRequestContext* context;
};

struct HttpStreamEvent {
    explicit HttpStreamEvent(HttpStreamEventType eventType) : type(eventType) {}
    HttpStreamEventType type;
    // Non-null only for BodyStart; never retain the request or its context.
    const HttpStreamRequest* request = nullptr;
    // Borrowed bytes, valid only during a BodyData callback.
    const uint8_t* data = nullptr;
    size_t length = 0U;
    HttpStreamAbortReason abortReason = HttpStreamAbortReason::ReceiveError;
};

using HttpStreamHandler = HttpStreamHandlerResult (*)(
    void* context, const HttpStreamEvent& event, WebResponseWriter& response
);

// Separate registration capability on the same physical server. The borrowed
// handler context must survive every callback until the server is stopped.
class HttpStreamingServerTransport {
public:
    virtual bool addStreamingRoute(const char* path, HttpMethod method,
                                   HttpStreamHandler handler, void* context,
                                   size_t maxContentLength) = 0;

protected:
    ~HttpStreamingServerTransport() = default;
};

} // namespace Web
} // namespace AquaCore
