#include "DoserMultipartFirmwareParser.h"

#include <limits.h>
#include <string.h>

namespace {
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }
bool token(const char* a, size_t n, const char* b) {
    size_t i = 0U;
    for (; i < n && b[i] != 0; ++i) if (lower(a[i]) != lower(b[i])) return false;
    return i == n && b[i] == 0;
}
bool bchar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '\'' || c == '(' || c == ')' ||
           c == '+' || c == '_' || c == ',' || c == '-' || c == '.' ||
           c == '/' || c == ':' || c == '=' || c == '?' || c == ' ';
}
}

bool DoserMultipartFirmwareParser::equalInsensitive(const char* text, size_t length,
                                                     const char* literal) {
    return token(text, length, literal);
}

bool DoserMultipartFirmwareParser::parseContentType(const char* value, char boundary[71]) {
    if (value == nullptr || boundary == nullptr) return false;
    boundary[0] = 0;
    const size_t length = strnlen(value, 128U);
    if (length == 0U || length >= 128U) return false;
    const char* semi = static_cast<const char*>(memchr(value, ';', length));
    if (semi == nullptr || !token(value, static_cast<size_t>(semi - value), "multipart/form-data"))
        return false;
    const char* p = semi + 1;
    while (*p == ' ' || *p == '\t') ++p;
    const char* eq = strchr(p, '=');
    if (eq == nullptr || !token(p, static_cast<size_t>(eq - p), "boundary")) return false;
    p = eq + 1;
    const bool quoted = *p == '"';
    if (quoted) ++p;
    size_t n = 0U;
    while (*p != 0 && (!quoted || *p != '"')) {
        if (n == 70U || !bchar(*p)) return false;
        boundary[n++] = *p++;
    }
    if (quoted && *p++ != '"') return false;
    if (*p != 0 || n == 0U || boundary[n - 1U] == ' ') return false;
    boundary[n] = 0;
    return true;
}

bool DoserMultipartFirmwareParser::parseFirmwareSize(const char* value, uint32_t& size) {
    if (value == nullptr) return false;
    const size_t n = strnlen(value, 11U);
    if (n == 0U || n > 10U) return false;
    uint32_t result = 0U;
    for (size_t i = 0U; i < n; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(value[i] - '0');
        if (result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    if (result == 0U) return false;
    size = result;
    return true;
}

bool DoserMultipartFirmwareParser::validContentLength(size_t body, uint32_t firmwareSize) {
    return firmwareSize != 0U && body > firmwareSize &&
           static_cast<uint64_t>(body) <= static_cast<uint64_t>(firmwareSize) + 664U;
}

bool DoserMultipartFirmwareParser::begin(const char* boundary) {
    if (boundary == nullptr) return false;
    boundaryLength_ = strnlen(boundary, 71U);
    if (boundaryLength_ == 0U || boundaryLength_ > 70U) return false;
    memcpy(boundary_, boundary, boundaryLength_ + 1U);
    opening_[0] = '-'; opening_[1] = '-';
    memcpy(opening_ + 2U, boundary_, boundaryLength_);
    opening_[boundaryLength_ + 2U] = '\r'; opening_[boundaryLength_ + 3U] = '\n';
    openingLength_ = boundaryLength_ + 4U;
    closing_[0] = '\r'; closing_[1] = '\n'; closing_[2] = '-'; closing_[3] = '-';
    memcpy(closing_ + 4U, boundary_, boundaryLength_);
    closing_[boundaryLength_ + 4U] = '-'; closing_[boundaryLength_ + 5U] = '-';
    closingLength_ = boundaryLength_ + 6U;
    memcpy(nextPart_, closing_, boundaryLength_ + 4U);
    nextPart_[boundaryLength_ + 4U] = '\r'; nextPart_[boundaryLength_ + 5U] = '\n';
    nextPartLength_ = boundaryLength_ + 6U;
    position_ = headerLength_ = carryLength_ = closeTail_ = 0U;
    endEmitted_ = false;
    filename_[0] = 0;
    state_ = State::Opening;
    return true;
}

bool DoserMultipartFirmwareParser::parseHeaders() {
    bool disposition = false, type = false;
    size_t pos = 0U;
    while (pos < headerLength_) {
        const size_t begin = pos;
        while (pos + 1U < headerLength_ && !(headers_[pos] == '\r' && headers_[pos + 1U] == '\n')) ++pos;
        if (pos + 1U >= headerLength_ || pos + 2U - begin > 256U) return false;
        const size_t n = pos - begin;
        if (n == 0U) return disposition && filename_[0] != 0;
        const char* line = headers_ + begin;
        if (n >= 21U && token(line, 20U, "Content-Disposition:")) {
            if (disposition) return false;
            disposition = true;
            // Only the two required quoted parameters are accepted, in either order.
            if (n < 30U || memcmp(line + 20U, " form-data", 10U) != 0) return false;
            size_t cursor = 30U;
            bool name = false, filename = false;
            while (cursor < n) {
                if (line[cursor++] != ';') return false;
                while (cursor < n && line[cursor] == ' ') ++cursor;
                const size_t key = cursor;
                while (cursor < n && line[cursor] != '=') ++cursor;
                if (cursor == n || ++cursor == n || line[cursor++] != '"') return false;
                const size_t value = cursor;
                while (cursor < n && line[cursor] != '"') ++cursor;
                if (cursor == n) return false;
                const size_t valueLength = cursor++ - value;
                if (token(line + key, value - key - 2U, "name")) {
                    if (name || !token(line + value, valueLength, "firmware")) return false;
                    name = true;
                } else if (token(line + key, value - key - 2U, "filename")) {
                    if (filename || valueLength == 0U || valueLength > 128U) return false;
                    for (size_t i = 0U; i < valueLength; ++i) {
                        const unsigned char c = static_cast<unsigned char>(line[value + i]);
                        if (c < 32U || c == 127U || c == '/' || c == '\\') return false;
                        filename_[i] = static_cast<char>(c);
                    }
                    filename_[valueLength] = 0;
                    if (valueLength < 4U ||
                        !token(filename_ + valueLength - 4U, 4U, ".bin")) return false;
                    filename = true;
                } else return false;
            }
            if (!name || !filename) return false;
        } else if (n >= 14U && token(line, 13U, "Content-Type:")) {
            if (type || n == 13U || n > 256U) return false;
            type = true;
            if (line[13U] != ' ' || n == 14U ||
                (n >= 24U && token(line + 14U, 10U, "multipart/"))) return false;
            for (size_t i = 14U; i < n; ++i)
                if (static_cast<unsigned char>(line[i]) < 33U || line[i] == ';') return false;
        } else return false;
        pos += 2U;
    }
    return false;
}

bool DoserMultipartFirmwareParser::emitRange(const uint8_t* bytes, size_t from, size_t to,
                                              size_t oldCarry, Sink sink, void* context) {
    if (from >= to) return true;
    if (from < oldCarry) {
        const size_t end = to < oldCarry ? to : oldCarry;
        if (!sink(context, Event::Data, carry_ + from, end - from)) return false;
        from = end;
    }
    while (from < to) {
        const size_t count = to - from > 1024U ? 1024U : to - from;
        if (!sink(context, Event::Data, bytes + from - oldCarry, count)) return false;
        from += count;
    }
    return true;
}

bool DoserMultipartFirmwareParser::feedData(const uint8_t* bytes, size_t length,
                                             Sink sink, void* context) {
    const size_t old = carryLength_;
    const size_t total = old + length;
    const auto at = [&](size_t i) -> uint8_t { return i < old ? carry_[i] : bytes[i - old]; };
    size_t emit = 0U;
    for (size_t i = 0U; i < total; ++i) {
        const size_t remaining = total - i;
        const size_t closeCompare = remaining < closingLength_ ? remaining : closingLength_;
        const size_t nextCompare = remaining < nextPartLength_ ? remaining : nextPartLength_;
        bool closePrefix = true, nextPrefix = true;
        for (size_t j = 0U; j < closeCompare; ++j)
            if (at(i + j) != closing_[j]) { closePrefix = false; break; }
        for (size_t j = 0U; j < nextCompare; ++j)
            if (at(i + j) != nextPart_[j]) { nextPrefix = false; break; }
        if (closePrefix && remaining >= closingLength_) {
            if (!emitRange(bytes, emit, i, old, sink, context)) return false;
            state_ = State::Closing;
            carryLength_ = 0U;
            return feed(bytes + (i + closingLength_ > old ? i + closingLength_ - old : 0U),
                        total - i - closingLength_, sink, context);
        }
        if (nextPrefix && remaining >= nextPartLength_) return false;
        if (closePrefix || nextPrefix) {
            if (!emitRange(bytes, emit, i, old, sink, context)) return false;
            if (remaining > sizeof(carry_)) return false;
            for (size_t j = 0U; j < remaining; ++j) carry_[j] = at(i + j);
            carryLength_ = remaining;
            return true;
        }
    }
    if (!emitRange(bytes, emit, total, old, sink, context)) return false;
    carryLength_ = 0U;
    return true;
}

bool DoserMultipartFirmwareParser::feed(const uint8_t* bytes, size_t length,
                                        Sink sink, void* context) {
    if ((bytes == nullptr && length != 0U) || sink == nullptr || state_ == State::Error) return false;
    size_t i = 0U;
    bool valid = true;
    while (i < length) {
        if (state_ == State::Opening) {
            if (bytes[i++] != opening_[position_++]) { valid = false; break; }
            if (position_ == openingLength_) { position_ = 0U; state_ = State::Headers; }
        } else if (state_ == State::Headers) {
            if (headerLength_ >= 512U) { valid = false; break; }
            const char c = static_cast<char>(bytes[i++]);
            if (c == '\n' && (headerLength_ == 0U || headers_[headerLength_ - 1U] != '\r')) { valid = false; break; }
            if (headerLength_ != 0U && headers_[headerLength_ - 1U] == '\r' && c != '\n') { valid = false; break; }
            headers_[headerLength_++] = c;
            if (headerLength_ >= 4U && memcmp(headers_ + headerLength_ - 4U, "\r\n\r\n", 4U) == 0) {
                if (!parseHeaders() || !sink(context, Event::Start, nullptr, 0U)) { valid = false; break; }
                state_ = State::Data;
            }
        } else if (state_ == State::Data) {
            if (!feedData(bytes + i, length - i, sink, context)) { valid = false; break; }
            return true;
        } else if (state_ == State::Closing) {
            const uint8_t expected = closeTail_ == 0U ? '\r' : '\n';
            if (bytes[i++] != expected) { valid = false; break; }
            if (++closeTail_ == 2U) state_ = State::Done;
        } else { valid = false; break; }
    }
    if (valid) return true;
    state_ = State::Error;
    return false;
}

bool DoserMultipartFirmwareParser::finish(Sink sink, void* context) {
    if (endEmitted_ || sink == nullptr ||
        (state_ != State::Done && !(state_ == State::Closing && closeTail_ == 0U))) {
        state_ = State::Error; return false;
    }
    state_ = State::Done;
    endEmitted_ = true;
    if (!sink(context, Event::End, nullptr, 0U)) {
        state_ = State::Error; return false;
    }
    return true;
}
