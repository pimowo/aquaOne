#include "AquaCore/Web/WebTypes.h"

#include <cstring>

namespace AquaCore {
namespace Web {

bool WebRequest::hasHeader(const char* name) const {
    return context != nullptr &&
        name != nullptr &&
        name[0] != '\0' &&
        context->hasHeader(name);
}

size_t WebRequest::copyHeader(
    const char* name,
    char* output,
    size_t outputSize
) const {
    if (
        context == nullptr ||
        name == nullptr ||
        name[0] == '\0' ||
        (output == nullptr && outputSize > 0U)
    ) {
        return 0U;
    }

    return context->copyHeader(name, output, outputSize);
}

bool WebRequest::authenticateBasic(
    const char* username,
    const char* password
) const {
    return context != nullptr &&
        username != nullptr &&
        password != nullptr &&
        context->authenticateBasic(username, password);
}

bool WebRequest::requestBasicAuthentication(const char* realm) const {
    return context != nullptr &&
        context->requestBasicAuthentication(realm);
}

bool WebResponseWriter::writeText(const char* text) {
    if (text == nullptr) {
        return false;
    }

    return write(text, std::strlen(text));
}

const char* contentTypeName(ContentType contentType) {
    switch (contentType) {
        case ContentType::Html:
            return "text/html; charset=utf-8";
        case ContentType::Json:
            return "application/json; charset=utf-8";
        case ContentType::PlainText:
            return "text/plain; charset=utf-8";
        case ContentType::Css:
            return "text/css; charset=utf-8";
        case ContentType::JavaScript:
            return "application/javascript; charset=utf-8";
    }

    return "application/octet-stream";
}

bool writeHtmlEscaped(
    WebResponseWriter& response,
    const char* text
) {
    if (text == nullptr) {
        return true;
    }

    const char* segment = text;
    for (const char* cursor = text; ; ++cursor) {
        const char* replacement = nullptr;
        switch (*cursor) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: break;
        }

        if (replacement != nullptr || *cursor == '\0') {
            if (
                cursor > segment &&
                !response.write(
                    segment,
                    static_cast<size_t>(cursor - segment)
                )
            ) {
                return false;
            }

            if (*cursor == '\0') {
                return true;
            }

            if (!response.writeText(replacement)) {
                return false;
            }
            segment = cursor + 1;
        }
    }
}

bool writeJsonString(
    WebResponseWriter& response,
    const char* text
) {
    if (!response.writeText("\"")) {
        return false;
    }

    if (text != nullptr) {
        static const char HEX[] = "0123456789abcdef";
        const char* segment = text;

        for (const char* cursor = text; ; ++cursor) {
            const unsigned char value =
                static_cast<unsigned char>(*cursor);
            const char* replacement = nullptr;

            switch (*cursor) {
                case '"': replacement = "\\\""; break;
                case '\\': replacement = "\\\\"; break;
                case '\b': replacement = "\\b"; break;
                case '\f': replacement = "\\f"; break;
                case '\n': replacement = "\\n"; break;
                case '\r': replacement = "\\r"; break;
                case '\t': replacement = "\\t"; break;
                default: break;
            }

            const bool control =
                value > 0U && value < 0x20U;

            if (
                replacement != nullptr ||
                control ||
                *cursor == '\0'
            ) {
                if (
                    cursor > segment &&
                    !response.write(
                        segment,
                        static_cast<size_t>(cursor - segment)
                    )
                ) {
                    return false;
                }

                if (*cursor == '\0') {
                    break;
                }

                if (control && replacement == nullptr) {
                    char encoded[6] = {
                        '\\', 'u', '0', '0',
                        HEX[(value >> 4U) & 0x0FU],
                        HEX[value & 0x0FU]
                    };
                    if (!response.write(encoded, sizeof(encoded))) {
                        return false;
                    }
                } else if (!response.writeText(replacement)) {
                    return false;
                }

                segment = cursor + 1;
            }
        }
    }

    return response.writeText("\"");
}

} // namespace Web
} // namespace AquaCore
