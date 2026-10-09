#include "AquaCore/Web/HttpBasicAuth.h"

#include <string.h>
#include <stdint.h>

namespace AquaCore { namespace Web {
namespace {
int decodeDigit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
bool equalScheme(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    return a == b;
}
} // namespace

bool verifyHttpBasicAuthorization(const char* header, size_t length,
                                  const char* user, const char* password) {
    if (header == nullptr || user == nullptr || password == nullptr ||
        length < 10U || length > HTTP_AUTHORIZATION_HEADER_CAPACITY)
        return false;
    const char prefix[] = "basic ";
    for (size_t i = 0U; i < 6U; ++i)
        if (!equalScheme(header[i], prefix[i])) return false;
    const size_t encoded = length - 6U;
    if ((encoded & 3U) != 0U) return false;
    struct DecodedCredentials {
        uint8_t bytes[189U] {};
        ~DecodedCredentials() {
            volatile uint8_t* wipe = bytes;
            for (size_t i = 0U; i < sizeof(bytes); ++i) wipe[i] = 0U;
        }
    } decoded;
    size_t used = 0U;
    for (size_t i = 6U; i < length; i += 4U) {
        const bool final = i + 4U == length;
        const int a = decodeDigit(header[i]);
        const int b = decodeDigit(header[i + 1U]);
        const int c = header[i + 2U] == '=' ? -2 : decodeDigit(header[i + 2U]);
        const int d = header[i + 3U] == '=' ? -2 : decodeDigit(header[i + 3U]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (!final && (c < 0 || d < 0)) ||
            (c == -2 && d != -2) ||
            (c == -2 && (b & 15) != 0) ||
            (d == -2 && c >= 0 && (c & 3) != 0)) return false;
        const size_t count = c == -2 ? 1U : (d == -2 ? 2U : 3U);
        if (used + count > sizeof(decoded.bytes)) return false;
        decoded.bytes[used++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        if (count > 1U)
            decoded.bytes[used++] = static_cast<uint8_t>((b << 4) | (c >> 2));
        if (count > 2U)
            decoded.bytes[used++] = static_cast<uint8_t>((c << 6) | d);
    }
    size_t colon = 0U;
    while (colon < used && decoded.bytes[colon] != ':') ++colon;
    if (colon == used) return false;
    const size_t userLength = strlen(user);
    const size_t passwordLength = strlen(password);
    const bool lengthsMatch = userLength == colon &&
                              passwordLength == used - colon - 1U;
    unsigned difference = 0U;
    if (lengthsMatch) {
        for (size_t i = 0U; i < userLength; ++i)
            difference |= decoded.bytes[i] ^ static_cast<uint8_t>(user[i]);
        for (size_t i = 0U; i < passwordLength; ++i)
            difference |= decoded.bytes[colon + 1U + i] ^ static_cast<uint8_t>(password[i]);
    }
    return lengthsMatch && difference == 0U;
}

bool validHttpBasicRealm(const char* realm) {
    if (realm == nullptr) return false;
    size_t length = 0U;
    for (; realm[length] != '\0'; ++length) {
        const unsigned char c = static_cast<unsigned char>(realm[length]);
        if (length == HTTP_BASIC_REALM_CAPACITY || c < 0x20U || c > 0x7eU ||
            c == '"' || c == '\\') return false;
    }
    return length != 0U;
}
} }
