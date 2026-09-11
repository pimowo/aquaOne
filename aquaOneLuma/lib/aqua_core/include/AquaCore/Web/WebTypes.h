#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Web {

enum class HttpMethod : uint8_t { Get = 0U, Post };
enum class ContentType : uint8_t { Html = 0U, Json, PlainText, Css, JavaScript };

struct WebRequest {
    HttpMethod method = HttpMethod::Get;
    const char* path = nullptr;
    const char* body = nullptr;
    size_t bodyLength = 0U;
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
