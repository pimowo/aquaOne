#include <unity.h>

#include "AquaCore/Web/HttpRouteRegistry.h"

using AquaCore::Web::HttpMethod;
using AquaCore::Web::HttpRouteRegistry;
using AquaCore::Web::HttpRouteRequest;
using AquaCore::Web::WebResponseWriter;

namespace {

void route(void*, const HttpRouteRequest&, WebResponseWriter&) {}
void notFound(void*, WebResponseWriter&) {}

void test_registration_and_duplicates() {
    HttpRouteRegistry registry;
    int context = 7;
    TEST_ASSERT_TRUE(registry.addRoute("/same", HttpMethod::Get, route, &context));
    TEST_ASSERT_TRUE(registry.addRoute("/same", HttpMethod::Post, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/same", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_EQUAL_UINT32(2U, registry.size());
    TEST_ASSERT_EQUAL_PTR(&context, registry.routeAt(0U)->context);
    TEST_ASSERT_EQUAL_STRING("/same", registry.routeAt(0U)->path);
    TEST_ASSERT_NULL(registry.routeAt(2U));
}

void test_invalid_path_and_handler() {
    HttpRouteRegistry registry;
    char longest[HttpRouteRegistry::MAX_PATH_LENGTH + 1U] {};
    longest[0] = '/';
    for (size_t i = 1U; i < HttpRouteRegistry::MAX_PATH_LENGTH; ++i) {
        longest[i] = 'x';
    }
    TEST_ASSERT_FALSE(registry.addRoute(nullptr, HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("bad", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/bad\r", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/bad\n", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/bad space", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/valid", HttpMethod::Get, nullptr, nullptr));
    TEST_ASSERT_FALSE(registry.addRoute("/valid", static_cast<HttpMethod>(99), route, nullptr));
    TEST_ASSERT_TRUE(registry.addRoute(longest, HttpMethod::Get, route, nullptr));
    longest[1] = 'Y';
    TEST_ASSERT_EQUAL_CHAR('x', registry.routeAt(0U)->path[1U]);
    char tooLong[HttpRouteRegistry::MAX_PATH_LENGTH + 2U] {};
    tooLong[0] = '/';
    for (size_t i = 1U; i <= HttpRouteRegistry::MAX_PATH_LENGTH; ++i) {
        tooLong[i] = 'x';
    }
    TEST_ASSERT_FALSE(registry.addRoute(tooLong, HttpMethod::Post, route, nullptr));
}

void test_capacity_and_freeze() {
    HttpRouteRegistry registry;
    char path[] = "/a";
    for (size_t i = 0U; i < HttpRouteRegistry::MAX_ROUTES; ++i) {
        path[1] = static_cast<char>('a' + i);
        TEST_ASSERT_TRUE(registry.addRoute(path, HttpMethod::Get, route, nullptr));
    }
    TEST_ASSERT_FALSE(registry.addRoute("/extra", HttpMethod::Get, route, nullptr));
    TEST_ASSERT_TRUE(registry.setNotFoundHandler(notFound, nullptr));
    TEST_ASSERT_FALSE(registry.setNotFoundHandler(notFound, nullptr));
    registry.freeze();
    TEST_ASSERT_TRUE(registry.isFrozen());
    TEST_ASSERT_FALSE(registry.addRoute("/late", HttpMethod::Post, route, nullptr));
    TEST_ASSERT_FALSE(registry.setNotFoundHandler(notFound, nullptr));
    // A stopped owner later reuses this frozen route table for a new begin.
    TEST_ASSERT_EQUAL_UINT32(HttpRouteRegistry::MAX_ROUTES, registry.size());
}

} // namespace

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "AquaCore/Web/EspIdfWebTransport.h"

// ESP32 build/link proof only. No Wi-Fi or HIL startup in this test.
volatile bool startTransportProof = false;
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_registration_and_duplicates);
    RUN_TEST(test_invalid_path_and_handler);
    RUN_TEST(test_capacity_and_freeze);
    AquaCore::Web::EspIdfWebTransport transport;
    TEST_ASSERT_TRUE(transport.addRoute("/proof", HttpMethod::Get, route));
    if (startTransportProof) {
        TEST_ASSERT_TRUE(transport.begin(80U));
    }
    transport.stop();
    UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registration_and_duplicates);
    RUN_TEST(test_invalid_path_and_handler);
    RUN_TEST(test_capacity_and_freeze);
    return UNITY_END();
}
#endif
