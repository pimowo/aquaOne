#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include <unity.h>

#include <stdint.h>
#include <string.h>
#include <type_traits>

#include "AquaCore/System/SystemService.h"
#include "AquaCore/Version.h"

using namespace AquaCore;

#if defined(ARDUINO_ARCH_ESP32)
static_assert(
    std::is_default_constructible<SystemService>::value,
    "SystemService must remain default-constructible on ESP32"
);
#else
static_assert(
    !std::is_default_constructible<SystemService>::value,
    "SystemService must require an injected backend off ESP32"
);
#endif

void setUp() {
}

void tearDown() {
}

namespace {

class FakeSystemBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override {
        return nowMs;
    }

    RestartReason restartReason() const override {
        return reason;
    }

    uint32_t nowMs = 0U;
    RestartReason reason = RestartReason::Unknown;
};

DeviceIdentity identity(
    const char* type = "lighting-controller",
    const char* name = "LumaSense",
    const char* firmware = "1.2.3",
    const char* hardware = "LOLIN32_TEST"
) {
    return DeviceIdentity(type, name, firmware, hardware);
}

void test_device_identity_is_stored_correctly() {
    FakeSystemBackend backend;
    SystemService service(backend);
    const DeviceIdentity expected = identity();

    TEST_ASSERT_TRUE(service.begin(expected));

    const DeviceIdentity& actual = service.deviceIdentity();
    TEST_ASSERT_EQUAL_STRING("lighting-controller", actual.deviceType);
    TEST_ASSERT_EQUAL_STRING("LumaSense", actual.deviceName);
    TEST_ASSERT_EQUAL_STRING("1.2.3", actual.firmwareVersion);
    TEST_ASSERT_EQUAL_STRING("LOLIN32_TEST", actual.hardwareVariant);
    TEST_ASSERT_TRUE(service.isReady());
}

void test_aqua_core_version_is_0_6_2() {
    FakeSystemBackend backend;
    SystemService service(backend);

    TEST_ASSERT_EQUAL_UINT(0U, VERSION_MAJOR);
    TEST_ASSERT_EQUAL_UINT(6U, VERSION_MINOR);
    TEST_ASSERT_EQUAL_UINT(2U, VERSION_PATCH);
    TEST_ASSERT_EQUAL_STRING("0.6.2", AQUA_CORE_VERSION);
    TEST_ASSERT_EQUAL_STRING("0.6.2", service.aquaCoreVersion());
}

void test_uptime_increases() {
    FakeSystemBackend backend;
    SystemService service(backend);
    TEST_ASSERT_TRUE(service.begin(identity()));

    backend.nowMs = 100U;
    const uint32_t before = service.uptimeMs();
    backend.nowMs = 250U;
    const uint32_t after = service.uptimeMs();

    TEST_ASSERT_GREATER_THAN_UINT32(before, after);
}

void test_uptime_handles_millis_overflow() {
    FakeSystemBackend backend;
    SystemService service(backend);
    TEST_ASSERT_TRUE(service.begin(identity()));

    backend.nowMs = UINT32_MAX - 2U;
    const uint32_t before = service.uptimeMs();
    backend.nowMs = 1U;
    const uint32_t after = service.uptimeMs();

    TEST_ASSERT_EQUAL_UINT32(
        4U,
        static_cast<uint32_t>(after - before)
    );
}

void test_system_service_returns_backend_restart_reason() {
    FakeSystemBackend backend;
    backend.reason = RestartReason::Watchdog;
    SystemService service(backend);

    TEST_ASSERT_TRUE(service.begin(identity()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RestartReason::Watchdog),
        static_cast<int>(service.restartReason())
    );
}

void test_restart_reason_names() {
    TEST_ASSERT_EQUAL_STRING("POWER_ON", restartReasonName(RestartReason::PowerOn));
    TEST_ASSERT_EQUAL_STRING("SOFTWARE", restartReasonName(RestartReason::Software));
    TEST_ASSERT_EQUAL_STRING("WATCHDOG", restartReasonName(RestartReason::Watchdog));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", restartReasonName(RestartReason::Brownout));
    TEST_ASSERT_EQUAL_STRING("DEEP_SLEEP", restartReasonName(RestartReason::DeepSleep));
    TEST_ASSERT_EQUAL_STRING("PANIC", restartReasonName(RestartReason::Panic));
    TEST_ASSERT_EQUAL_STRING("OTHER", restartReasonName(RestartReason::Other));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", restartReasonName(RestartReason::Unknown));
}

void test_begin_does_not_modify_input_identity() {
    FakeSystemBackend backend;
    SystemService service(backend);
    DeviceIdentity input = identity();
    const DeviceIdentity before = input;

    TEST_ASSERT_TRUE(service.begin(input));
    TEST_ASSERT_EQUAL_MEMORY(
        &before,
        &input,
        sizeof(DeviceIdentity)
    );
}

void test_services_keep_independent_device_identities() {
    FakeSystemBackend firstBackend;
    FakeSystemBackend secondBackend;
    SystemService first(firstBackend);
    SystemService second(secondBackend);

    TEST_ASSERT_TRUE(first.begin(
        identity(
            "lighting-controller",
            "LumaSense",
            "1.0.0",
            "LOLIN32_TEST"
        )
    ));
    TEST_ASSERT_TRUE(second.begin(
        identity(
            "dosing-controller",
            "AquaDoser",
            "2.0.0",
            "DOSER_V1"
        )
    ));

    TEST_ASSERT_EQUAL_STRING(
        "LumaSense",
        first.deviceIdentity().deviceName
    );
    TEST_ASSERT_EQUAL_STRING(
        "AquaDoser",
        second.deviceIdentity().deviceName
    );
    TEST_ASSERT_NOT_EQUAL(
        first.deviceIdentity().deviceName,
        second.deviceIdentity().deviceName
    );
}

} // namespace

void runTests() {
    RUN_TEST(test_device_identity_is_stored_correctly);
    RUN_TEST(test_aqua_core_version_is_0_6_2);
    RUN_TEST(test_uptime_increases);
    RUN_TEST(test_uptime_handles_millis_overflow);
    RUN_TEST(test_system_service_returns_backend_restart_reason);
    RUN_TEST(test_restart_reason_names);
    RUN_TEST(test_begin_does_not_modify_input_identity);
    RUN_TEST(test_services_keep_independent_device_identities);
}

#if defined(ARDUINO)

void setup() {
    delay(2000);
    UNITY_BEGIN();
    runTests();
    UNITY_END();
}

void loop() {
}

#else

int main() {
    UNITY_BEGIN();
    runTests();
    return UNITY_END();
}

#endif
