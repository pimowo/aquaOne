#include <Arduino.h>
#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/Config/Crc32.h"
#include "AquaCore/Config/StorageBackend.h"
#include "AquaCore/Config/StorageRecord.h"
#include "AquaCore/Config/StorageService.h"

using AquaCore::Config::StorageBackend;
using AquaCore::Config::StorageService;
namespace Record = AquaCore::Config::StorageRecord;

namespace {

constexpr size_t BLOB_CAPACITY = 256U;
constexpr uint16_t TEST_SCHEMA = 7U;

struct TestPayload {
    uint32_t marker;
    int32_t value;
    uint8_t enabled;
    uint8_t reserved[3];
};

struct OtherPayload {
    uint8_t bytes[5];
};

class MockStorageBackend final : public StorageBackend {
public:
    struct Blob {
        uint8_t data[BLOB_CAPACITY] {};
        size_t length = 0U;
    };

    bool begin(const char* storageNamespace) override {
        if (!beginResult || storageNamespace == nullptr) {
            return false;
        }

        opened = true;
        strncpy(
            selectedNamespace,
            storageNamespace,
            sizeof(selectedNamespace) - 1U
        );
        selectedNamespace[sizeof(selectedNamespace) - 1U] = '\0';
        return true;
    }

    void end() override {
        opened = false;
    }

    size_t blobLength(const char* key) override {
        return opened ? blob(key).length : 0U;
    }

    size_t readBlob(
        const char* key,
        void* output,
        size_t maximumLength
    ) override {
        if (!opened || output == nullptr) {
            return 0U;
        }

        if (failReadAfterWritePending) {
            failReadAfterWritePending = false;
            return 0U;
        }

        const Blob& source = blob(key);
        if (
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
        if (
            !opened ||
            input == nullptr ||
            length > BLOB_CAPACITY
        ) {
            return 0U;
        }

        if (failNextWrite) {
            failNextWrite = false;
            return 0U;
        }

        Blob& destination = blob(key);

        if (partialWriteArmed) {
            size_t written = partialWriteLength;
            if (written > length) {
                written = length;
            }
            memcpy(destination.data, input, written);
            destination.length = written;
            partialWriteArmed = false;
            return written;
        }

        memcpy(destination.data, input, length);
        destination.length = length;

        if (corruptAfterWriteArmed && corruptOffset < length) {
            destination.data[corruptOffset] ^= 0x5AU;
            corruptAfterWriteArmed = false;
        }

        if (failReadAfterWriteArmed) {
            failReadAfterWriteArmed = false;
            failReadAfterWritePending = true;
        }

        return length;
    }

    Blob& raw(const char* key) {
        return blob(key);
    }

    bool beginResult = true;
    bool opened = false;
    bool failNextWrite = false;
    bool partialWriteArmed = false;
    size_t partialWriteLength = 0U;
    bool corruptAfterWriteArmed = false;
    size_t corruptOffset = 0U;
    bool failReadAfterWriteArmed = false;
    bool failReadAfterWritePending = false;
    char selectedNamespace[32] {};

private:
    Blob& blob(const char* key) {
        return key != nullptr && strcmp(key, "b") == 0
            ? slots_[1]
            : slots_[0];
    }

    Blob slots_[2] {};
};

bool validateTestPayload(const void* data, size_t size) {
    if (data == nullptr || size != sizeof(TestPayload)) {
        return false;
    }

    const TestPayload& payload =
        *static_cast<const TestPayload*>(data);
    return
        payload.marker == 0xA5C40F17UL &&
        payload.value >= 0 &&
        payload.value <= 100 &&
        payload.enabled <= 1U;
}

bool validateOtherPayload(const void* data, size_t size) {
    return data != nullptr && size == sizeof(OtherPayload);
}

TestPayload payload(int32_t value) {
    TestPayload result {};
    result.marker = 0xA5C40F17UL;
    result.value = value;
    result.enabled = 1U;
    return result;
}

uint16_t read16(const uint8_t* data) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8U)
    );
}

uint32_t read32(const uint8_t* data) {
    return
        static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8U) |
        (static_cast<uint32_t>(data[2]) << 16U) |
        (static_cast<uint32_t>(data[3]) << 24U);
}

void write16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8U);
}

void write32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8U);
    data[2] = static_cast<uint8_t>(value >> 16U);
    data[3] = static_cast<uint8_t>(value >> 24U);
}

void updateHeaderCrc(MockStorageBackend::Blob& blob) {
    write32(
        blob.data + Record::HEADER_CRC_OFFSET,
        AquaCore::Config::crc32(
            blob.data,
            Record::HEADER_CRC_INPUT_SIZE
        )
    );
}

void updatePayloadAndHeaderCrc(MockStorageBackend::Blob& blob) {
    const uint32_t payloadLength = read32(
        blob.data + Record::PAYLOAD_LENGTH_OFFSET
    );
    write32(
        blob.data + Record::PAYLOAD_CRC_OFFSET,
        AquaCore::Config::crc32(
            blob.data + Record::HEADER_SIZE,
            payloadLength
        )
    );
    updateHeaderCrc(blob);
}

void assertPayloadValue(
    StorageService& service,
    int32_t expected
) {
    TestPayload loaded {};
    TEST_ASSERT_TRUE(service.load(&loaded));
    TEST_ASSERT_EQUAL_INT32(expected, loaded.value);
}

void test_empty_storage_load_false() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(
        sizeof(TestPayload),
        TEST_SCHEMA,
        validateTestPayload
    ));

    TestPayload destination = payload(41);
    TEST_ASSERT_FALSE(service.load(&destination));
    TEST_ASSERT_FALSE(service.hasValidPayload());
}

void test_valid_save_and_load_blob() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(
        sizeof(TestPayload),
        TEST_SCHEMA,
        validateTestPayload
    ));

    const TestPayload input = payload(27);
    TestPayload output {};
    TEST_ASSERT_TRUE(service.save(&input));
    TEST_ASSERT_TRUE(service.load(&output));
    TEST_ASSERT_EQUAL_MEMORY(&input, &output, sizeof(input));
}

void test_one_valid_slot() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(
        sizeof(TestPayload),
        TEST_SCHEMA,
        validateTestPayload
    ));
    TestPayload input = payload(10);
    TEST_ASSERT_TRUE(service.save(&input));
    TEST_ASSERT_GREATER_THAN_UINT32(0U, backend.raw("a").length);
    TEST_ASSERT_EQUAL_UINT32(0U, backend.raw("b").length);
    assertPayloadValue(service, 10);
}

void test_a_corrupt_b_valid() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload first = payload(11);
    TestPayload second = payload(22);
    TEST_ASSERT_TRUE(service.save(&first));
    TEST_ASSERT_TRUE(service.save(&second));
    backend.raw("a").data[Record::HEADER_SIZE] ^= 0x44U;
    assertPayloadValue(service, 22);
}

void test_b_corrupt_a_valid() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload first = payload(11);
    TestPayload second = payload(22);
    TEST_ASSERT_TRUE(service.save(&first));
    TEST_ASSERT_TRUE(service.save(&second));
    backend.raw("b").data[Record::HEADER_SIZE] ^= 0x44U;
    assertPayloadValue(service, 11);
}

void test_both_valid_newer_generation_wins() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload first = payload(31);
    TestPayload second = payload(32);
    TEST_ASSERT_TRUE(service.save(&first));
    TEST_ASSERT_TRUE(service.save(&second));
    assertPayloadValue(service, 32);
}

void test_generation_rollover() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload before = payload(40);
    TestPayload after = payload(41);
    TEST_ASSERT_TRUE(service.save(&before));

    MockStorageBackend::Blob& slotA = backend.raw("a");
    write32(
        slotA.data + Record::GENERATION_OFFSET,
        UINT32_MAX
    );
    updateHeaderCrc(slotA);

    TEST_ASSERT_TRUE(service.save(&after));
    TEST_ASSERT_EQUAL_UINT32(
        0U,
        read32(
            backend.raw("b").data +
            Record::GENERATION_OFFSET
        )
    );
    assertPayloadValue(service, 41);
}

void test_bad_magic() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(1);
    TEST_ASSERT_TRUE(service.save(&input));
    write32(backend.raw("a").data + Record::MAGIC_OFFSET, 0x11223344UL);
    updateHeaderCrc(backend.raw("a"));
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_bad_format_version() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(2);
    TEST_ASSERT_TRUE(service.save(&input));
    write16(
        backend.raw("a").data + Record::FORMAT_VERSION_OFFSET,
        Record::FORMAT_VERSION + 1U
    );
    updateHeaderCrc(backend.raw("a"));
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_bad_schema_version() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(3);
    TEST_ASSERT_TRUE(service.save(&input));
    write16(
        backend.raw("a").data + Record::SCHEMA_VERSION_OFFSET,
        TEST_SCHEMA + 1U
    );
    updateHeaderCrc(backend.raw("a"));
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_bad_payload_length() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(4);
    TEST_ASSERT_TRUE(service.save(&input));
    write32(
        backend.raw("a").data + Record::PAYLOAD_LENGTH_OFFSET,
        sizeof(TestPayload) - 1U
    );
    updateHeaderCrc(backend.raw("a"));
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_bad_header_crc() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(5);
    TEST_ASSERT_TRUE(service.save(&input));
    backend.raw("a").data[Record::HEADER_CRC_OFFSET] ^= 0x01U;
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_bad_payload_crc() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(6);
    TEST_ASSERT_TRUE(service.save(&input));
    backend.raw("a").data[Record::HEADER_SIZE] ^= 0x01U;
    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_semantic_validator_rejects_payload() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(7);
    TEST_ASSERT_TRUE(service.save(&input));

    TestPayload invalid = payload(101);
    memcpy(
        backend.raw("a").data + Record::HEADER_SIZE,
        &invalid,
        sizeof(invalid)
    );
    updatePayloadAndHeaderCrc(backend.raw("a"));

    TestPayload output {};
    TEST_ASSERT_FALSE(service.load(&output));
}

void test_partial_write_preserves_previous_record() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload previous = payload(20);
    TestPayload next = payload(21);
    TEST_ASSERT_TRUE(service.save(&previous));
    backend.partialWriteArmed = true;
    backend.partialWriteLength = Record::HEADER_SIZE - 1U;
    TEST_ASSERT_FALSE(service.save(&next));
    assertPayloadValue(service, 20);
}

void test_write_failure_preserves_previous_record() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload previous = payload(30);
    TestPayload next = payload(31);
    TEST_ASSERT_TRUE(service.save(&previous));
    backend.failNextWrite = true;
    TEST_ASSERT_FALSE(service.save(&next));
    assertPayloadValue(service, 30);
}

void test_readback_verification_failure_is_reported() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload input = payload(50);
    backend.failReadAfterWriteArmed = true;
    TEST_ASSERT_FALSE(service.save(&input));
}

void test_multiple_saves_alternate_slots() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload first = payload(1);
    TestPayload second = payload(2);
    TestPayload third = payload(3);
    TEST_ASSERT_TRUE(service.save(&first));
    TEST_ASSERT_TRUE(service.save(&second));
    TEST_ASSERT_TRUE(service.save(&third));
    TEST_ASSERT_EQUAL_UINT32(
        3U,
        read32(backend.raw("a").data + Record::GENERATION_OFFSET)
    );
    TEST_ASSERT_EQUAL_UINT32(
        2U,
        read32(backend.raw("b").data + Record::GENERATION_OFFSET)
    );
    assertPayloadValue(service, 3);
}

void test_latest_payload_survives_service_recreation() {
    MockStorageBackend backend;
    {
        StorageService first(backend, "ac4", "a", "b");
        TEST_ASSERT_TRUE(first.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
        TestPayload one = payload(60);
        TestPayload two = payload(61);
        TEST_ASSERT_TRUE(first.save(&one));
        TEST_ASSERT_TRUE(first.save(&two));
    }

    StorageService restarted(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(restarted.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    assertPayloadValue(restarted, 61);
}

void test_save_does_not_modify_source_payload() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload source = payload(70);
    const TestPayload before = source;
    TEST_ASSERT_TRUE(service.save(&source));
    TEST_ASSERT_EQUAL_MEMORY(&before, &source, sizeof(source));
}

void test_load_failure_does_not_modify_destination() {
    MockStorageBackend backend;
    StorageService service(backend, "ac4", "a", "b");
    TEST_ASSERT_TRUE(service.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TestPayload destination = payload(71);
    const TestPayload before = destination;
    TEST_ASSERT_FALSE(service.load(&destination));
    TEST_ASSERT_EQUAL_MEMORY(&before, &destination, sizeof(destination));
}

void test_arbitrary_non_lumasense_payload_can_be_stored() {
    MockStorageBackend backend;
    StorageService service(backend, "other", "a", "b");
    TEST_ASSERT_TRUE(service.begin(
        sizeof(OtherPayload),
        99U,
        validateOtherPayload
    ));

    const OtherPayload source {{1U, 3U, 5U, 7U, 9U}};
    OtherPayload destination {};
    TEST_ASSERT_TRUE(service.save(&source));
    TEST_ASSERT_TRUE(service.load(&destination));
    TEST_ASSERT_EQUAL_MEMORY(&source, &destination, sizeof(source));
}

void test_independent_services_use_independent_locations() {
    MockStorageBackend firstBackend;
    MockStorageBackend secondBackend;
    StorageService first(firstBackend, "device-one", "a", "b");
    StorageService second(secondBackend, "device-two", "a", "b");

    TEST_ASSERT_TRUE(first.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));
    TEST_ASSERT_TRUE(second.begin(sizeof(TestPayload), TEST_SCHEMA, validateTestPayload));

    TestPayload firstPayload = payload(80);
    TestPayload secondPayload = payload(81);
    TEST_ASSERT_TRUE(first.save(&firstPayload));
    TEST_ASSERT_TRUE(second.save(&secondPayload));

    TEST_ASSERT_EQUAL_STRING("device-one", firstBackend.selectedNamespace);
    TEST_ASSERT_EQUAL_STRING("device-two", secondBackend.selectedNamespace);
    assertPayloadValue(first, 80);
    assertPayloadValue(second, 81);
}

void test_crc_matches_legacy_lumasense_result() {
    const char input[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(
        0xCBF43926UL,
        AquaCore::Config::crc32(input, 9U)
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

    RUN_TEST(test_empty_storage_load_false);
    RUN_TEST(test_valid_save_and_load_blob);
    RUN_TEST(test_one_valid_slot);
    RUN_TEST(test_a_corrupt_b_valid);
    RUN_TEST(test_b_corrupt_a_valid);
    RUN_TEST(test_both_valid_newer_generation_wins);
    RUN_TEST(test_generation_rollover);
    RUN_TEST(test_bad_magic);
    RUN_TEST(test_bad_format_version);
    RUN_TEST(test_bad_schema_version);
    RUN_TEST(test_bad_payload_length);
    RUN_TEST(test_bad_header_crc);
    RUN_TEST(test_bad_payload_crc);
    RUN_TEST(test_semantic_validator_rejects_payload);
    RUN_TEST(test_partial_write_preserves_previous_record);
    RUN_TEST(test_write_failure_preserves_previous_record);
    RUN_TEST(test_readback_verification_failure_is_reported);
    RUN_TEST(test_multiple_saves_alternate_slots);
    RUN_TEST(test_latest_payload_survives_service_recreation);
    RUN_TEST(test_save_does_not_modify_source_payload);
    RUN_TEST(test_load_failure_does_not_modify_destination);
    RUN_TEST(test_arbitrary_non_lumasense_payload_can_be_stored);
    RUN_TEST(test_independent_services_use_independent_locations);
    RUN_TEST(test_crc_matches_legacy_lumasense_result);

    UNITY_END();
}

void loop() {
}
