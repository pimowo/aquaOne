#include <Arduino.h>
#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/Config/StorageBackend.h"
#include "AquaCore/Config/StorageService.h"
#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/Diagnostics/DiagnosticsTypes.h"
#include "AquaCore/System/SystemBackend.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Time/NtpService.h"
#include "AquaCore/Time/RtcBus.h"
#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Version.h"

using namespace AquaCore;
using namespace AquaCore::Config;
using namespace AquaCore::Diagnostics;
using namespace AquaCore::Time;

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

class FakeRtcBus final : public RtcBus {
public:
    bool begin(int, int) override {
        ++beginCalls;
        return beginResult;
    }

    bool readRegisters(
        uint8_t,
        uint8_t startRegister,
        uint8_t* output,
        size_t length
    ) override {
        ++readCalls;
        if (!readResult || output == nullptr) {
            return false;
        }

        memcpy(output, &registers[startRegister], length);
        return true;
    }

    bool writeRegisters(
        uint8_t,
        uint8_t startRegister,
        const uint8_t* input,
        size_t length
    ) override {
        ++writeCalls;
        if (!writeResult || input == nullptr) {
            return false;
        }

        memcpy(&registers[startRegister], input, length);
        return true;
    }

    uint8_t registers[256] {};
    bool beginResult = true;
    bool readResult = true;
    bool writeResult = true;
    uint16_t beginCalls = 0U;
    uint16_t readCalls = 0U;
    uint16_t writeCalls = 0U;
};

class FakeNtpBackend final : public NtpBackend {
public:
    bool start(
        const char* const[NTP_MAX_SERVERS],
        uint8_t
    ) override {
        ++startCalls;
        return startResult;
    }

    NtpBackendResult poll(UtcDateTime& output) override {
        ++pollCalls;
        output = utc;
        return pollResult;
    }

    void stop() override {
        ++stopCalls;
    }

    bool startResult = true;
    NtpBackendResult pollResult = NtpBackendResult::Pending;
    UtcDateTime utc {};
    uint16_t startCalls = 0U;
    uint16_t pollCalls = 0U;
    uint16_t stopCalls = 0U;
};

class MockStorageBackend final : public StorageBackend {
public:
    struct Blob {
        uint8_t data[128] {};
        size_t length = 0U;
    };

    bool begin(const char*) override {
        ++beginCalls;
        opened = beginResult;
        return opened;
    }

    void end() override {
        opened = false;
    }

    size_t blobLength(const char* key) override {
        ++lengthCalls;
        return opened ? blob(key).length : 0U;
    }

    size_t readBlob(
        const char* key,
        void* output,
        size_t maximumLength
    ) override {
        ++readCalls;
        const Blob& source = blob(key);
        if (
            !opened ||
            output == nullptr ||
            source.length == 0U ||
            source.length > maximumLength
        ) {
            return 0U;
        }

        memcpy(output, source.data, source.length);
        return source.length;
    }

    size_t writeBlob(
        const char* key,
        const void* input,
        size_t length
    ) override {
        ++writeCalls;
        if (
            !opened ||
            !writeResult ||
            input == nullptr ||
            length > sizeof(Blob::data)
        ) {
            return 0U;
        }

        Blob& destination = blob(key);
        memcpy(destination.data, input, length);
        destination.length = length;
        return length;
    }

    bool beginResult = true;
    bool writeResult = true;
    bool opened = false;
    uint16_t beginCalls = 0U;
    uint16_t lengthCalls = 0U;
    uint16_t readCalls = 0U;
    uint16_t writeCalls = 0U;

private:
    Blob& blob(const char* key) {
        return key != nullptr && strcmp(key, "b") == 0
            ? slots_[1]
            : slots_[0];
    }

    Blob slots_[2] {};
};

struct TestPayload {
    uint32_t marker = 0xAC500001UL;
    uint32_t value = 0U;
};

bool validatePayload(const void* data, size_t size) {
    if (data == nullptr || size != sizeof(TestPayload)) {
        return false;
    }

    return static_cast<const TestPayload*>(data)->marker ==
        0xAC500001UL;
}

RtcConfig rtcConfig() {
    RtcConfig config {};
    config.sdaPin = 32;
    config.sclPin = 33;
    return config;
}

uint8_t bcd(uint8_t value) {
    return static_cast<uint8_t>(
        ((value / 10U) << 4U) | (value % 10U)
    );
}

void fillRtc(FakeRtcBus& bus, bool osf = false) {
    bus.registers[0x00] = bcd(5U);
    bus.registers[0x01] = bcd(4U);
    bus.registers[0x02] = bcd(12U);
    bus.registers[0x03] = bcd(1U);
    bus.registers[0x04] = bcd(9U);
    bus.registers[0x05] = bcd(9U);
    bus.registers[0x06] = bcd(26U);
    bus.registers[0x0F] = osf ? 0x80U : 0U;
}

struct Fixture {
    Fixture()
        : system(systemBackend),
          rtc(rtcBus, rtcConfig()),
          ntp(rtc, ntpBackend),
          storage(storageBackend, "ac5", "a", "b") {
        fillRtc(rtcBus);
    }

    void beginSystem(
        const char* name = "LumaSense",
        RestartReason reason = RestartReason::PowerOn
    ) {
        systemBackend.reason = reason;
        TEST_ASSERT_TRUE(system.begin(DeviceIdentity(
            "lighting-controller",
            name,
            "5.0.0",
            "LOLIN32_TEST"
        )));
    }

    void beginValidRtc() {
        fillRtc(rtcBus, false);
        TEST_ASSERT_TRUE(rtc.begin());
        TEST_ASSERT_TRUE(rtc.read().valid);
    }

    void beginInvalidRtc() {
        fillRtc(rtcBus, true);
        TEST_ASSERT_TRUE(rtc.begin());
        TEST_ASSERT_FALSE(rtc.isValid());
    }

    void beginStorage(bool withPayload) {
        TEST_ASSERT_TRUE(storage.begin(
            sizeof(TestPayload),
            1U,
            validatePayload
        ));

        if (withPayload) {
            TestPayload value {};
            value.value = 42U;
            TEST_ASSERT_TRUE(storage.save(&value));
        }
    }

    FakeSystemBackend systemBackend;
    SystemService system;
    FakeRtcBus rtcBus;
    RtcService rtc;
    FakeNtpBackend ntpBackend;
    NtpService ntp;
    MockStorageBackend storageBackend;
    StorageService storage;
};

DiagnosticsService diagnostics(
    Fixture& fixture,
    const NtpService* ntp = nullptr
) {
    return DiagnosticsService(
        fixture.system,
        fixture.rtc,
        fixture.storage,
        "Europe/Warsaw",
        ntp
    );
}

void prepareHealthy(Fixture& fixture) {
    fixture.beginSystem();
    fixture.beginValidRtc();
    fixture.beginStorage(true);
}

void test_snapshot_contains_device_identity() {
    Fixture fixture;
    prepareHealthy(fixture);
    DiagnosticsService service = diagnostics(fixture);

    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_EQUAL_STRING(
        "lighting-controller",
        value.system.identity.deviceType
    );
    TEST_ASSERT_EQUAL_STRING(
        "LumaSense",
        value.system.identity.deviceName
    );
    TEST_ASSERT_EQUAL_STRING(
        "5.0.0",
        value.system.identity.firmwareVersion
    );
    TEST_ASSERT_EQUAL_STRING(
        "LOLIN32_TEST",
        value.system.identity.hardwareVariant
    );
}

void test_snapshot_contains_aqua_core_version() {
    Fixture fixture;
    prepareHealthy(fixture);
    DiagnosticsService service = diagnostics(fixture);
    TEST_ASSERT_EQUAL_STRING(
        AQUA_CORE_VERSION,
        service.snapshot().system.aquaCoreVersion
    );
}

void test_uptime_is_forwarded() {
    Fixture fixture;
    prepareHealthy(fixture);
    fixture.systemBackend.nowMs = 123456U;
    DiagnosticsService service = diagnostics(fixture);
    TEST_ASSERT_EQUAL_UINT32(
        123456U,
        service.snapshot().system.uptimeMs
    );
}

void test_restart_reason_is_forwarded() {
    Fixture fixture;
    fixture.beginSystem("LumaSense", RestartReason::Watchdog);
    fixture.beginValidRtc();
    fixture.beginStorage(true);
    DiagnosticsService service = diagnostics(fixture);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RestartReason::Watchdog),
        static_cast<uint8_t>(
            service.snapshot().system.restartReason
        )
    );
}

void test_valid_rtc_has_ok_time_health() {
    Fixture fixture;
    prepareHealthy(fixture);
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_TRUE(value.time.rtcReady);
    TEST_ASSERT_TRUE(value.time.rtcValid);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(TimeState::Valid),
        static_cast<uint8_t>(value.time.state)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Ok),
        static_cast<uint8_t>(value.timeHealth)
    );
}

void test_invalid_rtc_has_warning_time_health() {
    Fixture fixture;
    fixture.beginSystem();
    fixture.beginInvalidRtc();
    fixture.beginStorage(true);
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(TimeState::Invalid),
        static_cast<uint8_t>(value.time.state)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Warning),
        static_cast<uint8_t>(value.timeHealth)
    );
}

void test_ntp_in_progress_is_reported() {
    Fixture fixture;
    prepareHealthy(fixture);
    TEST_ASSERT_TRUE(fixture.ntp.begin(0U));
    TEST_ASSERT_TRUE(fixture.ntp.requestSync(true, 100U));
    DiagnosticsService service = diagnostics(fixture, &fixture.ntp);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_TRUE(value.time.ntpAvailable);
    TEST_ASSERT_TRUE(value.time.ntpInitialized);
    TEST_ASSERT_TRUE(value.time.ntpSyncInProgress);
}

void test_ntp_success_is_reported() {
    Fixture fixture;
    prepareHealthy(fixture);
    TEST_ASSERT_TRUE(fixture.ntp.begin(0U));
    TEST_ASSERT_TRUE(fixture.ntp.requestSync(true, 100U));
    fixture.ntpBackend.pollResult = NtpBackendResult::Success;
    fixture.ntpBackend.utc = {2026U, 9U, 9U, 12U, 0U, 0U};
    fixture.ntp.update(true, 200U);
    fixture.systemBackend.nowMs = 1200U;

    DiagnosticsService service = diagnostics(fixture, &fixture.ntp);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NtpSyncResult::Success),
        static_cast<uint8_t>(value.time.lastSyncResult)
    );
    TEST_ASSERT_TRUE(value.time.hasLastSuccessfulSyncAge);
    TEST_ASSERT_EQUAL_UINT32(
        1000U,
        value.time.lastSuccessfulSyncAgeMs
    );
}

void test_ntp_failure_is_reported() {
    Fixture fixture;
    prepareHealthy(fixture);
    TEST_ASSERT_TRUE(fixture.ntp.begin(0U));
    TEST_ASSERT_TRUE(fixture.ntp.requestSync(true, 100U));
    fixture.ntpBackend.pollResult = NtpBackendResult::Failure;
    fixture.ntp.update(true, 200U);

    DiagnosticsService service = diagnostics(fixture, &fixture.ntp);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NtpSyncResult::Failure),
        static_cast<uint8_t>(
            service.snapshot().time.lastSyncResult
        )
    );
}

void test_ready_storage_with_payload_is_ok() {
    Fixture fixture;
    prepareHealthy(fixture);
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_TRUE(value.storage.backendReady);
    TEST_ASSERT_TRUE(value.storage.hasValidPayload);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Ok),
        static_cast<uint8_t>(value.storageHealth)
    );
}

void test_ready_storage_without_payload_is_warning() {
    Fixture fixture;
    fixture.beginSystem();
    fixture.beginValidRtc();
    fixture.beginStorage(false);
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_TRUE(value.storage.backendReady);
    TEST_ASSERT_FALSE(value.storage.hasValidPayload);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Warning),
        static_cast<uint8_t>(value.storageHealth)
    );
}

void test_storage_backend_failure_is_error() {
    Fixture fixture;
    fixture.beginSystem();
    fixture.beginValidRtc();
    fixture.storageBackend.beginResult = false;
    TEST_ASSERT_FALSE(fixture.storage.begin(
        sizeof(TestPayload),
        1U,
        validatePayload
    ));
    DiagnosticsService service = diagnostics(fixture);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Error),
        static_cast<uint8_t>(
            service.snapshot().storageHealth
        )
    );
}

void test_load_and_save_results_are_reported() {
    Fixture fixture;
    prepareHealthy(fixture);
    TestPayload loaded {};
    TEST_ASSERT_TRUE(fixture.storage.load(&loaded));
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StorageOperationResult::Success),
        static_cast<uint8_t>(value.storage.lastLoadResult)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StorageOperationResult::Success),
        static_cast<uint8_t>(value.storage.lastSaveResult)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StorageSlot::A),
        static_cast<uint8_t>(value.storage.activeSlot)
    );
    TEST_ASSERT_EQUAL_UINT32(1U, value.storage.activeGeneration);
}

void test_overall_health_is_worst_module_health() {
    Fixture fixture;
    fixture.beginSystem();
    fixture.beginInvalidRtc();
    fixture.storageBackend.beginResult = false;
    TEST_ASSERT_FALSE(fixture.storage.begin(
        sizeof(TestPayload),
        1U,
        validatePayload
    ));
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HealthState::Error),
        static_cast<uint8_t>(value.overallHealth)
    );
}

void test_diagnostics_does_not_modify_system_service() {
    Fixture fixture;
    prepareHealthy(fixture);
    const DeviceIdentity before = fixture.system.deviceIdentity();
    const RestartReason reasonBefore = fixture.system.restartReason();
    DiagnosticsService service = diagnostics(fixture);
    service.snapshot();

    TEST_ASSERT_TRUE(fixture.system.isReady());
    TEST_ASSERT_EQUAL_MEMORY(
        &before,
        &fixture.system.deviceIdentity(),
        sizeof(DeviceIdentity)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(reasonBefore),
        static_cast<uint8_t>(fixture.system.restartReason())
    );
}

void test_diagnostics_does_not_modify_time_services() {
    Fixture fixture;
    prepareHealthy(fixture);
    TEST_ASSERT_TRUE(fixture.ntp.begin(0U));
    TEST_ASSERT_TRUE(fixture.ntp.requestSync(true, 10U));

    const uint16_t rtcReads = fixture.rtcBus.readCalls;
    const uint16_t rtcWrites = fixture.rtcBus.writeCalls;
    const uint16_t ntpStarts = fixture.ntpBackend.startCalls;
    const uint16_t ntpPolls = fixture.ntpBackend.pollCalls;
    const uint16_t ntpStops = fixture.ntpBackend.stopCalls;

    DiagnosticsService service = diagnostics(fixture, &fixture.ntp);
    service.snapshot();

    TEST_ASSERT_EQUAL_UINT16(rtcReads, fixture.rtcBus.readCalls);
    TEST_ASSERT_EQUAL_UINT16(rtcWrites, fixture.rtcBus.writeCalls);
    TEST_ASSERT_EQUAL_UINT16(ntpStarts, fixture.ntpBackend.startCalls);
    TEST_ASSERT_EQUAL_UINT16(ntpPolls, fixture.ntpBackend.pollCalls);
    TEST_ASSERT_EQUAL_UINT16(ntpStops, fixture.ntpBackend.stopCalls);
    TEST_ASSERT_TRUE(fixture.ntp.isSyncInProgress());
}

void test_diagnostics_does_not_modify_storage_service() {
    Fixture fixture;
    prepareHealthy(fixture);
    const uint16_t lengthCalls = fixture.storageBackend.lengthCalls;
    const uint16_t readCalls = fixture.storageBackend.readCalls;
    const uint16_t writeCalls = fixture.storageBackend.writeCalls;

    DiagnosticsService service = diagnostics(fixture);
    service.snapshot();

    TEST_ASSERT_EQUAL_UINT16(
        lengthCalls,
        fixture.storageBackend.lengthCalls
    );
    TEST_ASSERT_EQUAL_UINT16(
        readCalls,
        fixture.storageBackend.readCalls
    );
    TEST_ASSERT_EQUAL_UINT16(
        writeCalls,
        fixture.storageBackend.writeCalls
    );
}

void test_two_diagnostics_services_are_independent() {
    Fixture first;
    Fixture second;
    first.beginSystem("LumaSense");
    second.beginSystem("AquaDoser");
    first.beginValidRtc();
    second.beginValidRtc();
    first.beginStorage(true);
    second.beginStorage(true);
    first.systemBackend.nowMs = 100U;
    second.systemBackend.nowMs = 200U;

    DiagnosticsService firstService = diagnostics(first);
    DiagnosticsService secondService = diagnostics(second);
    const DiagnosticsSnapshot firstValue = firstService.snapshot();
    const DiagnosticsSnapshot secondValue = secondService.snapshot();

    TEST_ASSERT_EQUAL_STRING(
        "LumaSense",
        firstValue.system.identity.deviceName
    );
    TEST_ASSERT_EQUAL_STRING(
        "AquaDoser",
        secondValue.system.identity.deviceName
    );
    TEST_ASSERT_EQUAL_UINT32(100U, firstValue.system.uptimeMs);
    TEST_ASSERT_EQUAL_UINT32(200U, secondValue.system.uptimeMs);
}

void test_optional_ntp_can_be_absent() {
    Fixture fixture;
    prepareHealthy(fixture);
    DiagnosticsService service = diagnostics(fixture, nullptr);
    const DiagnosticsSnapshot value = service.snapshot();
    TEST_ASSERT_FALSE(value.time.ntpAvailable);
    TEST_ASSERT_FALSE(value.time.ntpSyncInProgress);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NtpSyncResult::NotAvailable),
        static_cast<uint8_t>(value.time.lastSyncResult)
    );
}

void test_snapshot_is_generic_and_has_no_lumasense_types() {
    Fixture fixture;
    fixture.beginSystem("AquaDoser");
    fixture.beginValidRtc();
    fixture.beginStorage(true);
    DiagnosticsService service = diagnostics(fixture);
    const DiagnosticsSnapshot value = service.snapshot();

    TEST_ASSERT_EQUAL_STRING(
        "AquaDoser",
        value.system.identity.deviceName
    );
    TEST_ASSERT_EQUAL_STRING(
        "Europe/Warsaw",
        value.time.providerName
    );
    TEST_ASSERT_GREATER_THAN_UINT32(
        0U,
        static_cast<uint32_t>(sizeof(value))
    );
}

} // namespace

void setUp() {
}

void tearDown() {
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_snapshot_contains_device_identity);
    RUN_TEST(test_snapshot_contains_aqua_core_version);
    RUN_TEST(test_uptime_is_forwarded);
    RUN_TEST(test_restart_reason_is_forwarded);
    RUN_TEST(test_valid_rtc_has_ok_time_health);
    RUN_TEST(test_invalid_rtc_has_warning_time_health);
    RUN_TEST(test_ntp_in_progress_is_reported);
    RUN_TEST(test_ntp_success_is_reported);
    RUN_TEST(test_ntp_failure_is_reported);
    RUN_TEST(test_ready_storage_with_payload_is_ok);
    RUN_TEST(test_ready_storage_without_payload_is_warning);
    RUN_TEST(test_storage_backend_failure_is_error);
    RUN_TEST(test_load_and_save_results_are_reported);
    RUN_TEST(test_overall_health_is_worst_module_health);
    RUN_TEST(test_diagnostics_does_not_modify_system_service);
    RUN_TEST(test_diagnostics_does_not_modify_time_services);
    RUN_TEST(test_diagnostics_does_not_modify_storage_service);
    RUN_TEST(test_two_diagnostics_services_are_independent);
    RUN_TEST(test_optional_ntp_can_be_absent);
    RUN_TEST(test_snapshot_is_generic_and_has_no_lumasense_types);

    UNITY_END();
}

void loop() {
}
