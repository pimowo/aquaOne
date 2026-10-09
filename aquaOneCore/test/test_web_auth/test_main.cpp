#include <unity.h>

#include "AquaCore/Web/HttpBasicAuth.h"
#include "AquaCore/Web/HttpRouteRegistry.h"

#include <cstring>

using namespace AquaCore::Web;

namespace {
class Context final : public WebRequestContext {
public:
    bool hasHeader(const char* name) const override {
        return header != nullptr && std::strcmp(name, "Authorization") == 0;
    }
    size_t copyHeader(const char* name, char* out, size_t capacity) const override {
        if (out != nullptr && capacity != 0U) out[0] = '\0';
        if (!hasHeader(name)) return 0U;
        const size_t length = std::strlen(header);
        if (out != nullptr && capacity > length)
            std::memcpy(out, header, length + 1U);
        return length;
    }
    bool authenticateBasic(const char* user, const char* password) const override {
        return header != nullptr && verifyHttpBasicAuthorization(
            header, std::strlen(header), user, password);
    }
    bool requestBasicAuthentication(const char* realm) const override {
        if (!validHttpBasicRealm(realm)) return false;
        challenged = true;
        std::strncpy(lastRealm, realm, sizeof(lastRealm) - 1U);
        status = 401U;
        return true;
    }
    const char* header = nullptr;
    mutable bool challenged = false;
    mutable uint16_t status = 0U;
    mutable char lastRealm[64U] {};
};

void test_header_access_and_capacity() {
    Context context;
    const HttpRouteRequest request {HttpMethod::Get, "/update", nullptr, 0U,
                                    &context};
    TEST_ASSERT_FALSE(request.hasHeader("Authorization"));
    char out[HTTP_AUTHORIZATION_HEADER_CAPACITY + 1U] {};
    size_t length = 99U;
    TEST_ASSERT_FALSE(request.copyHeader("Authorization", out, sizeof(out), length));
    TEST_ASSERT_EQUAL_UINT(0U, length);
    context.header = "Basic dTpw";
    TEST_ASSERT_TRUE(request.hasHeader("Authorization"));
    TEST_ASSERT_FALSE(request.copyHeader("Authorization", nullptr, 0U, length));
    TEST_ASSERT_EQUAL_UINT(10U, length);
    TEST_ASSERT_FALSE(request.copyHeader("Authorization", out, 0U, length));
    TEST_ASSERT_EQUAL_UINT(10U, length);
    TEST_ASSERT_FALSE(request.copyHeader("Authorization", out, 5U, length));
    TEST_ASSERT_EQUAL_UINT(10U, length);
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
    TEST_ASSERT_TRUE(request.copyHeader("Authorization", out, sizeof(out), length));
    TEST_ASSERT_EQUAL_STRING(context.header, out);
    TEST_ASSERT_EQUAL_UINT(10U, length);
    char exact[HTTP_AUTHORIZATION_HEADER_CAPACITY + 1U] {};
    std::memcpy(exact, "Basic ", 6U);
    std::memset(exact + 6U, 'A', HTTP_AUTHORIZATION_HEADER_CAPACITY - 6U);
    context.header = exact;
    TEST_ASSERT_TRUE(request.copyHeader("Authorization", out, sizeof(out), length));
    TEST_ASSERT_EQUAL_UINT(HTTP_AUTHORIZATION_HEADER_CAPACITY, length);
    char tooLong[HTTP_AUTHORIZATION_HEADER_CAPACITY + 2U] {};
    std::memcpy(tooLong, exact, HTTP_AUTHORIZATION_HEADER_CAPACITY);
    tooLong[HTTP_AUTHORIZATION_HEADER_CAPACITY] = 'A';
    context.header = tooLong;
    TEST_ASSERT_FALSE(request.copyHeader("Authorization", out, sizeof(out), length));
    TEST_ASSERT_EQUAL_UINT(HTTP_AUTHORIZATION_HEADER_CAPACITY + 1U, length);
    TEST_ASSERT_FALSE(request.authenticateBasic("u", "p"));
}

void test_basic_auth_validation() {
    Context context;
    const HttpRouteRequest request {HttpMethod::Get, "/update", nullptr, 0U,
                                    &context};
    TEST_ASSERT_FALSE(request.authenticateBasic("u", "p"));
    const char* rejected[] = {
        "Bearer dTpw", "Basic", "Basic @@@@", "Basic dTpw="
    };
    for (size_t i = 0U; i < 4U; ++i) {
        context.header = rejected[i];
        TEST_ASSERT_FALSE(request.authenticateBasic("u", "p"));
    }
    context.header = "Basic dTpw";
    TEST_ASSERT_FALSE(request.authenticateBasic("wrong", "p"));
    TEST_ASSERT_FALSE(request.authenticateBasic("u", "wrong"));
    TEST_ASSERT_TRUE(request.authenticateBasic("u", "p"));
    context.header = "bAsIc dTpw";
    TEST_ASSERT_TRUE(request.authenticateBasic("u", "p"));
}

void test_challenge_realm_validation() {
    Context context;
    const HttpRouteRequest request {HttpMethod::Get, "/update", nullptr, 0U,
                                    &context};
    TEST_ASSERT_FALSE(request.requestBasicAuthentication("bad\r\nInjected"));
    TEST_ASSERT_FALSE(context.challenged);
    TEST_ASSERT_FALSE(validHttpBasicRealm("bad\"quote"));
    TEST_ASSERT_FALSE(validHttpBasicRealm("bad\\slash"));
    TEST_ASSERT_TRUE(request.requestBasicAuthentication("PMW AquaDoser"));
    TEST_ASSERT_EQUAL_UINT(401U, context.status);
    TEST_ASSERT_EQUAL_STRING("PMW AquaDoser", context.lastRealm);
}

void test_exact_authorization_capacity_and_one_over() {
    const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char password[188U] {};
    std::memset(password, 'x', sizeof(password) - 1U);
    unsigned char credentials[189U] {};
    credentials[0] = 'u'; credentials[1] = ':';
    std::memset(credentials + 2U, 'x', sizeof(credentials) - 2U);
    char header[HTTP_AUTHORIZATION_HEADER_CAPACITY + 2U] {};
    std::memcpy(header, "Basic ", 6U);
    size_t out = 6U;
    for (size_t i = 0U; i < sizeof(credentials); i += 3U) {
        const unsigned value = (static_cast<unsigned>(credentials[i]) << 16U) |
            (static_cast<unsigned>(credentials[i + 1U]) << 8U) |
            credentials[i + 2U];
        header[out++] = alphabet[(value >> 18U) & 63U];
        header[out++] = alphabet[(value >> 12U) & 63U];
        header[out++] = alphabet[(value >> 6U) & 63U];
        header[out++] = alphabet[value & 63U];
    }
    TEST_ASSERT_EQUAL_UINT(HTTP_AUTHORIZATION_HEADER_CAPACITY, out);
    TEST_ASSERT_TRUE(verifyHttpBasicAuthorization(header, out, "u", password));
    header[out++] = 'A';
    TEST_ASSERT_FALSE(verifyHttpBasicAuthorization(header, out, "u", password));
}
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_header_access_and_capacity);
    RUN_TEST(test_basic_auth_validation);
    RUN_TEST(test_challenge_realm_validation);
    RUN_TEST(test_exact_authorization_capacity_and_one_over);
    return UNITY_END();
}
