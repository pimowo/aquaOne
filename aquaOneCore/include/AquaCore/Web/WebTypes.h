#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Web {

enum class HttpMethod : uint8_t { Get = 0U, Post };
enum class ContentType : uint8_t { Html = 0U, Json, PlainText, Css, JavaScript };

class WebRequestContext {
public:
    virtual ~WebRequestContext() = default;
    virtual bool hasHeader(const char* name) const = 0;
    virtual size_t copyHeader(
        const char* name,
        char* output,
        size_t outputSize
    ) const = 0;
    virtual bool authenticateBasic(
        const char* username,
        const char* password
    ) const = 0;
    virtual bool requestBasicAuthentication(const char* realm) const = 0;
};

struct WebRequest {
    // This non-owning view and its context are valid only during a callback.
    HttpMethod method = HttpMethod::Get;
    const char* path = nullptr;
    const char* body = nullptr;
    size_t bodyLength = 0U;
    const WebRequestContext* context = nullptr;

    bool hasHeader(const char* name) const;
    size_t copyHeader(
        const char* name,
        char* output,
        size_t outputSize
    ) const;
    bool authenticateBasic(
        const char* username,
        const char* password
    ) const;
    bool requestBasicAuthentication(const char* realm) const;
};

enum class WebUploadStatus : uint8_t { Start = 0U, Chunk, End, Abort };

struct WebUploadEvent {
    WebUploadStatus status = WebUploadStatus::Start;
    // All pointers are valid only for the duration of the upload callback.
    const char* filename = nullptr;
    const uint8_t* data = nullptr;
    size_t dataLength = 0U;
    size_t bytesReceived = 0U;
};

using WebUploadHandler = void (*)(
    void* context,
    const WebRequest& request,
    const WebUploadEvent& event
);

struct WebRouteOptions {
    // Zero preserves legacy behavior. This limit excludes multipart uploads.
    size_t maxBodyLength = 0U;
    WebUploadHandler uploadHandler = nullptr;
    void* uploadContext = nullptr;
};

class WebResponseWriter {
public:
    virtual ~WebResponseWriter() = default;
    virtual bool beginResponse(uint16_t statusCode, ContentType contentType) = 0;
    virtual bool write(const char* data, size_t length) = 0;
    virtual bool endResponse() = 0;
    bool writeText(const char* text);
};

using WebRouteHandler = void (*)(
    void* context,
    const WebRequest& request,
    WebResponseWriter& response
);

const char* contentTypeName(ContentType contentType);
bool writeHtmlEscaped(WebResponseWriter& response, const char* text);
bool writeJsonString(WebResponseWriter& response, const char* text);

} // namespace Web
} // namespace AquaCore
