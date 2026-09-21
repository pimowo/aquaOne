#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <AquaCore/Config/ConfigLifecycle.h>
#include <AquaCore/Config/Crc32.h>
#include <AquaCore/Config/StorageBackend.h>
#include <AquaCore/Config/StorageRecord.h>
#include <AquaCore/Config/StorageService.h>

using namespace AquaCore::Config;
namespace Record = AquaCore::Config::StorageRecord;

namespace {

constexpr size_t BUFFER_CAPACITY = 256U;
constexpr uint16_t CURRENT_SCHEMA = 3U;
constexpr char STORAGE_NAMESPACE[] = {'c', 'f', 'g', '\0'};
constexpr char SLOT_A_KEY[] = {'a', '\0'};
constexpr char SLOT_B_KEY[] = {'b', '\0'};

struct ConfigV1 {
    uint32_t level;
};

struct ConfigV2 {
    uint32_t level;
    uint32_t limit;
};

struct ExampleConfig {
    uint32_t level;
    uint32_t limit;
    uint32_t mode;
};

class FakeStorageBackend final : public StorageBackend {
public:
    struct Blob {
        uint8_t data[BUFFER_CAPACITY] {};
        size_t length = 0U;
    };

    bool begin(const char* storageNamespace) override {
        opened = storageNamespace != nullptr;
        return opened;
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
        ++writeCount;
        if (
            !opened ||
            input == nullptr ||
            length > BUFFER_CAPACITY ||
            failNextWrite
        ) {
            failNextWrite = false;
            return 0U;
        }

        Blob& destination = blob(key);
        memcpy(destination.data, input, length);
        destination.length = length;
        return length;
    }

    Blob& raw(const char* key) {
        return blob(key);
    }

    size_t writeCount = 0U;
    bool failNextWrite = false;
    bool opened = false;

private:
    Blob& blob(const char* key) {
        return key != nullptr && strcmp(key, SLOT_B_KEY) == 0
            ? slots_[1]
            : slots_[0];
    }

    Blob slots_[2] {};
};

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

void seedRecord(
    FakeStorageBackend& backend,
    const char* key,
    uint16_t schemaVersion,
    const void* payload,
    size_t payloadSize,
    uint32_t generation
) {
    FakeStorageBackend::Blob& blob = backend.raw(key);
    blob.length = Record::HEADER_SIZE + payloadSize;
    memset(blob.data, 0, blob.length);
    write32(blob.data + Record::MAGIC_OFFSET, Record::MAGIC);
    write16(
        blob.data + Record::FORMAT_VERSION_OFFSET,
        Record::FORMAT_VERSION
    );
    write16(
        blob.data + Record::SCHEMA_VERSION_OFFSET,
        schemaVersion
    );
    write32(
        blob.data + Record::PAYLOAD_LENGTH_OFFSET,
        static_cast<uint32_t>(payloadSize)
    );
    write32(
        blob.data + Record::GENERATION_OFFSET,
        generation
    );
    memcpy(blob.data + Record::HEADER_SIZE, payload, payloadSize);
    write32(
        blob.data + Record::PAYLOAD_CRC_OFFSET,
        crc32(payload, payloadSize)
    );
    write32(
        blob.data + Record::HEADER_CRC_OFFSET,
        crc32(blob.data, Record::HEADER_CRC_INPUT_SIZE)
    );
}

bool validateStoredCurrent(const void* payload, size_t size) {
    return payload != nullptr && size == sizeof(ExampleConfig);
}

struct CallTrace {
    char values[96] {};
    size_t size = 0U;

    void add(char value) {
        if (size < sizeof(values)) {
            values[size++] = value;
        }
    }

    int position(char value, size_t occurrence = 0U) const {
        size_t found = 0U;
        for (size_t index = 0U; index < size; ++index) {
            if (values[index] == value) {
                if (found == occurrence) {
                    return static_cast<int>(index);
                }
                ++found;
            }
        }
        return -1;
    }

    size_t count(char value) const {
        size_t result = 0U;
        for (size_t index = 0U; index < size; ++index) {
            if (values[index] == value) {
                ++result;
            }
        }
        return result;
    }
};

struct AdapterContext {
    CallTrace* trace = nullptr;
    ExampleConfig active {};
    bool activeValid = false;
    bool failMigration = false;
    bool failSemantic = false;
    bool failHardware = false;
    bool failApply = false;
    size_t applyCount = 0U;
    size_t commitCount = 0U;
};

bool buildDefaults(
    void* context,
    void* output,
    size_t capacity,
    size_t& outputSize
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('F');
    if (output == nullptr || capacity < sizeof(ExampleConfig)) {
        return false;
    }
    const ExampleConfig defaults {10U, 50U, 0U};
    memcpy(output, &defaults, sizeof(defaults));
    outputSize = sizeof(defaults);
    return true;
}

bool decodeConfig(
    void* context,
    uint16_t schemaVersion,
    const void* raw,
    size_t rawSize,
    void* output,
    size_t capacity,
    size_t& outputSize
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('D');
    size_t expectedSize = 0U;
    if (schemaVersion == 1U) {
        expectedSize = sizeof(ConfigV1);
    } else if (schemaVersion == 2U) {
        expectedSize = sizeof(ConfigV2);
    } else if (schemaVersion == CURRENT_SCHEMA) {
        expectedSize = sizeof(ExampleConfig);
    } else {
        return false;
    }
    if (
        raw == nullptr ||
        output == nullptr ||
        rawSize != expectedSize ||
        capacity < expectedSize
    ) {
        return false;
    }
    memcpy(output, raw, expectedSize);
    outputSize = expectedSize;
    return true;
}

bool migrateConfig(
    void* context,
    uint16_t fromSchema,
    const void* input,
    size_t inputSize,
    void* output,
    size_t capacity,
    size_t& outputSize
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('M');
    if (adapter.failMigration) {
        return false;
    }

    if (
        fromSchema == 1U &&
        inputSize == sizeof(ConfigV1) &&
        capacity >= sizeof(ConfigV2)
    ) {
        const ConfigV1& oldConfig =
            *static_cast<const ConfigV1*>(input);
        const ConfigV2 next {oldConfig.level, 50U};
        memcpy(output, &next, sizeof(next));
        outputSize = sizeof(next);
        return true;
    }

    if (
        fromSchema == 2U &&
        inputSize == sizeof(ConfigV2) &&
        capacity >= sizeof(ExampleConfig)
    ) {
        const ConfigV2& oldConfig =
            *static_cast<const ConfigV2*>(input);
        const ExampleConfig next {
            oldConfig.level,
            oldConfig.limit,
            0U
        };
        memcpy(output, &next, sizeof(next));
        outputSize = sizeof(next);
        return true;
    }
    return false;
}

bool validateCurrent(
    void* context,
    const void* candidate,
    size_t size
) {
    static_cast<AdapterContext*>(context)->trace->add('V');
    return candidate != nullptr && size == sizeof(ExampleConfig);
}

bool validateSemantic(
    void* context,
    const void* candidate,
    size_t size
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('S');
    if (adapter.failSemantic || size != sizeof(ExampleConfig)) {
        return false;
    }
    const ExampleConfig& config =
        *static_cast<const ExampleConfig*>(candidate);
    return config.level <= config.limit && config.mode <= 1U;
}

bool validateHardware(
    void* context,
    const void* candidate,
    size_t size
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('H');
    if (adapter.failHardware || size != sizeof(ExampleConfig)) {
        return false;
    }
    return static_cast<const ExampleConfig*>(candidate)->limit <= 100U;
}

bool encodeCurrent(
    void* context,
    const void* candidate,
    size_t candidateSize,
    void* raw,
    size_t capacity,
    size_t& rawSize
) {
    static_cast<AdapterContext*>(context)->trace->add('E');
    if (
        candidate == nullptr ||
        candidateSize != sizeof(ExampleConfig) ||
        raw == nullptr ||
        capacity < sizeof(ExampleConfig)
    ) {
        return false;
    }
    memcpy(raw, candidate, sizeof(ExampleConfig));
    rawSize = sizeof(ExampleConfig);
    return true;
}

bool applyConfig(
    void* context,
    const void* candidate,
    size_t size
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('A');
    ++adapter.applyCount;
    return
        !adapter.failApply &&
        candidate != nullptr &&
        size == sizeof(ExampleConfig);
}

void commitActive(
    void* context,
    const void* candidate,
    size_t size
) {
    AdapterContext& adapter = *static_cast<AdapterContext*>(context);
    adapter.trace->add('C');
    if (candidate != nullptr && size == sizeof(ExampleConfig)) {
        memcpy(&adapter.active, candidate, sizeof(adapter.active));
        adapter.activeValid = true;
        ++adapter.commitCount;
    }
}

ConfigApplyMode classifyConfig(
    void* context,
    const void* candidate,
    size_t size
) {
    static_cast<AdapterContext*>(context)->trace->add('R');
    if (
        candidate != nullptr &&
        size == sizeof(ExampleConfig) &&
        static_cast<const ExampleConfig*>(candidate)->mode == 1U
    ) {
        return ConfigApplyMode::RestartRequired;
    }
    return ConfigApplyMode::Live;
}

struct TracedStorage {
    ConfigStorage inner {};
    CallTrace* trace = nullptr;
};

ConfigLoadResult tracedLoad(
    void* context,
    void* raw,
    size_t capacity,
    ConfigLoadInfo& info
) {
    TracedStorage& storage = *static_cast<TracedStorage*>(context);
    storage.trace->add('L');
    return storage.inner.load(
        storage.inner.context,
        raw,
        capacity,
        info
    );
}

ConfigPersistenceResult tracedPersist(
    void* context,
    uint16_t schema,
    const void* raw,
    size_t size
) {
    TracedStorage& storage = *static_cast<TracedStorage*>(context);
    storage.trace->add('P');
    return storage.inner.persist(
        storage.inner.context,
        schema,
        raw,
        size
    );
}

struct TestRig {
    FakeStorageBackend backend;
    uint8_t storageBytes[BUFFER_CAPACITY] {};
    StorageService storage;
    CallTrace trace;
    AdapterContext adapterContext;
    TracedStorage tracedStorage;
    uint8_t rawBytes[64] {};
    uint8_t candidateABytes[64] {};
    uint8_t candidateBBytes[64] {};

    TestRig()
        : storage(
              backend,
              STORAGE_NAMESPACE,
              SLOT_A_KEY,
              SLOT_B_KEY,
              StorageWorkspace {
                  storageBytes,
                  sizeof(storageBytes)
              }
          ) {
        adapterContext.trace = &trace;
    }

    bool begin() {
        if (
            !storage.begin(
                sizeof(ExampleConfig),
                CURRENT_SCHEMA,
                validateStoredCurrent
            )
        ) {
            return false;
        }
        tracedStorage.inner = storageServiceConfigStorage(storage);
        tracedStorage.trace = &trace;
        return true;
    }

    ConfigStorage storagePlan() {
        ConfigStorage result {};
        result.context = &tracedStorage;
        result.load = tracedLoad;
        result.persist = tracedPersist;
        return result;
    }

    ConfigAdapter adapterPlan() {
        ConfigAdapter result {};
        result.context = &adapterContext;
        result.currentSchemaVersion = CURRENT_SCHEMA;
        result.maximumMigrationSteps = 4U;
        result.buildDefaults = buildDefaults;
        result.decode = decodeConfig;
        result.migrateStep = migrateConfig;
        result.validateCurrent = validateCurrent;
        result.validateSemantic = validateSemantic;
        result.validateHardware = validateHardware;
        result.encodeCurrent = encodeCurrent;
        result.apply = applyConfig;
        result.commitActive = commitActive;
        result.classify = classifyConfig;
        return result;
    }

    ConfigWorkspace workspace() {
        ConfigWorkspace result {};
        result.raw.data = rawBytes;
        result.raw.capacity = sizeof(rawBytes);
        result.candidateA.data = candidateABytes;
        result.candidateA.capacity = sizeof(candidateABytes);
        result.candidateB.data = candidateBBytes;
        result.candidateB.capacity = sizeof(candidateBBytes);
        return result;
    }
};

ExampleConfig config(
    uint32_t level,
    uint32_t limit = 50U,
    uint32_t mode = 0U
) {
    return ExampleConfig {level, limit, mode};
}

void assertOrdered(
    const CallTrace& trace,
    char before,
    char after
) {
    const int beforePosition = trace.position(before);
    const int afterPosition = trace.position(after);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, beforePosition);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, afterPosition);
    TEST_ASSERT_LESS_THAN_INT(afterPosition, beforePosition);
}

void assertStatusResult(
    const ConfigLifecycle& lifecycle,
    ConfigOperationResult expected
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(lifecycle.status().lastResult)
    );
}

void test_startup_current_loads_applies_without_rewrite() {
    TestRig rig;
    const ExampleConfig stored = config(20U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &stored,
        sizeof(stored),
        4U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(20U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Stored),
        static_cast<uint8_t>(lifecycle.status().source)
    );
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_TRUE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    assertOrdered(rig.trace, 'L', 'D');
    assertOrdered(rig.trace, 'D', 'V');
    assertOrdered(rig.trace, 'V', 'A');
    assertOrdered(rig.trace, 'A', 'C');
}

void test_startup_empty_applies_defaults_then_persists() {
    TestRig rig;
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::DefaultsApplied
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_TRUE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(1U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Defaults),
        static_cast<uint8_t>(lifecycle.status().source)
    );
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    assertOrdered(rig.trace, 'F', 'V');
    assertOrdered(rig.trace, 'V', 'A');
    assertOrdered(rig.trace, 'A', 'C');
    assertOrdered(rig.trace, 'C', 'P');
}

void test_startup_empty_persist_failure_keeps_active_defaults() {
    TestRig rig;
    TEST_ASSERT_TRUE(rig.begin());
    rig.backend.failNextWrite = true;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::PersistFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_TRUE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(1U, rig.adapterContext.commitCount);
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_FALSE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::BackendFailure),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
}

void test_startup_old_schema_migrates_stepwise_then_persists() {
    TestRig rig;
    const ConfigV1 stored {25U};
    seedRecord(rig.backend, SLOT_A_KEY, 1U, &stored, sizeof(stored), 9U);
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Migrated),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(2U, rig.trace.count('M'));
    TEST_ASSERT_EQUAL_UINT32(25U, rig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(1U, rig.backend.writeCount);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, rig.backend.raw(SLOT_B_KEY).length);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Migrated),
        static_cast<uint8_t>(lifecycle.status().source)
    );
    TEST_ASSERT_EQUAL_UINT16(
        CURRENT_SCHEMA,
        lifecycle.status().storedSchemaVersion
    );
    TEST_ASSERT_EQUAL_UINT16(
        CURRENT_SCHEMA,
        lifecycle.status().activeSchemaVersion
    );
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    assertOrdered(rig.trace, 'L', 'D');
    assertOrdered(rig.trace, 'D', 'M');
    assertOrdered(rig.trace, 'M', 'V');
    assertOrdered(rig.trace, 'V', 'A');
    assertOrdered(rig.trace, 'A', 'P');
}

void test_startup_migration_failure_does_not_apply_or_write() {
    TestRig rig;
    const ConfigV1 stored {25U};
    seedRecord(rig.backend, SLOT_A_KEY, 1U, &stored, sizeof(stored), 1U);
    TEST_ASSERT_TRUE(rig.begin());
    rig.adapterContext.failMigration = true;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::MigrationFailed
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_EQUAL_UINT16(1U, lifecycle.status().storedSchemaVersion);
}

void test_startup_migration_respects_maximum_steps() {
    TestRig rig;
    const ConfigV1 stored {25U};
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        1U,
        &stored,
        sizeof(stored),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigAdapter adapter = rig.adapterPlan();
    adapter.maximumMigrationSteps = 1U;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        adapter,
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::MigrationFailed
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(1U, rig.trace.count('M'));
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.raw(SLOT_B_KEY).length);
}
void test_startup_future_schema_is_preserved_and_rejected() {
    TestRig rig;
    const ExampleConfig stored = config(30U);
    seedRecord(rig.backend, SLOT_A_KEY, 4U, &stored, sizeof(stored), 2U);
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::UnsupportedVersion
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(4U, lifecycle.status().storedSchemaVersion);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::None),
        static_cast<uint8_t>(lifecycle.status().source)
    );
}

void test_startup_semantic_validation_failure_preserves_record() {
    TestRig rig;
    const ExampleConfig invalid = config(80U, 40U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &invalid,
        sizeof(invalid),
        2U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::ValidationFailed
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
}

void test_startup_apply_failure_does_not_publish_or_rewrite() {
    TestRig rig;
    const ConfigV1 stored {20U};
    seedRecord(rig.backend, SLOT_A_KEY, 1U, &stored, sizeof(stored), 2U);
    TEST_ASSERT_TRUE(rig.begin());
    rig.adapterContext.failApply = true;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
}

void test_startup_corrupt_requires_recovery_without_fallback() {
    TestRig rig;
    const ExampleConfig stored = config(22U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &stored,
        sizeof(stored),
        2U
    );
    rig.backend.raw(SLOT_A_KEY).data[Record::HEADER_SIZE] ^= 0x55U;
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::RecoveryRequired
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_FALSE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
}
void test_startup_corrupt_uses_recovery_defaults_without_write() {
    TestRig rig;
    const ExampleConfig stored = config(22U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &stored,
        sizeof(stored),
        2U
    );
    rig.backend.raw(SLOT_A_KEY).data[Record::HEADER_SIZE] ^= 0x55U;
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycleOptions options {};
    options.allowDefaultsForCorruptStorage = true;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace(),
        options
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::RecoveryRequired
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_TRUE(rig.adapterContext.activeValid);
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().hasStoredSchema);
    TEST_ASSERT_TRUE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Defaults),
        static_cast<uint8_t>(lifecycle.status().source)
    );
}

void test_overlapping_workspace_is_rejected() {
    TestRig rig;
    TEST_ASSERT_TRUE(rig.begin());
    ConfigWorkspace invalid = rig.workspace();
    invalid.candidateB = invalid.candidateA;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        invalid
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::InvalidArgument
        ),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
}

void test_insufficient_raw_workspace_capacity_is_rejected() {
    TestRig rig;
    const ExampleConfig stored = config(20U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &stored,
        sizeof(stored),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigWorkspace workspace = rig.workspace();
    workspace.raw.capacity = sizeof(ExampleConfig) - 1U;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        workspace
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::LoadFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
}
void test_runtime_live_change_persists_before_apply() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    rig.trace = {};

    const ExampleConfig proposal = config(20U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(
            lifecycle.update(&proposal, sizeof(proposal))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(20U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    assertOrdered(rig.trace, 'V', 'P');
    assertOrdered(rig.trace, 'P', 'R');
    assertOrdered(rig.trace, 'R', 'A');
    assertOrdered(rig.trace, 'A', 'C');
}

void test_runtime_identical_desired_maps_no_change_to_success() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::NoChange),
        static_cast<uint8_t>(
            lifecycle.update(&initial, sizeof(initial))
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::NoChange),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
}

void test_runtime_invalid_proposal_does_not_persist_or_apply() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const size_t applyCount = rig.adapterContext.applyCount;
    const ExampleConfig invalid = config(90U, 20U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::ValidationFailed
        ),
        static_cast<uint8_t>(
            lifecycle.update(&invalid, sizeof(invalid))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(
        applyCount,
        rig.adapterContext.applyCount
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigPersistenceResult::NotAttempted
        ),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
}

void test_runtime_persist_failure_does_not_apply() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const size_t applyCount = rig.adapterContext.applyCount;
    rig.backend.failNextWrite = true;
    const ExampleConfig proposal = config(30U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::PersistFailed),
        static_cast<uint8_t>(
            lifecycle.update(&proposal, sizeof(proposal))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(
        applyCount,
        rig.adapterContext.applyCount
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.trace.count('R'));
}

void test_runtime_apply_failure_keeps_old_active_and_desired() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    rig.adapterContext.failApply = true;
    const ExampleConfig proposal = config(35U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(
            lifecycle.update(&proposal, sizeof(proposal))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_EQUAL_UINT32(1U, rig.backend.writeCount);

    rig.adapterContext.failApply = false;
    ExampleConfig loaded {};
    TEST_ASSERT_TRUE(rig.storage.load(&loaded));
    TEST_ASSERT_EQUAL_UINT32(35U, loaded.level);
}

void test_runtime_restart_required_persists_without_live_apply() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const size_t applyCount = rig.adapterContext.applyCount;
    const ExampleConfig proposal = config(40U, 50U, 1U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigOperationResult::RestartRequired
        ),
        static_cast<uint8_t>(
            lifecycle.update(&proposal, sizeof(proposal))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(
        applyCount,
        rig.adapterContext.applyCount
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(1U, rig.trace.count('R'));
    assertOrdered(rig.trace, 'P', 'R');
}

void test_next_startup_retries_persisted_desired() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    rig.adapterContext.failApply = true;
    const ExampleConfig desired = config(45U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    const size_t applyCount = rig.adapterContext.applyCount;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(
        applyCount + 1U,
        rig.adapterContext.applyCount
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
}

void test_two_lifecycles_are_independent() {
    TestRig coreRig;
    TestRig domainRig;
    const ExampleConfig coreStored = config(11U);
    const ConfigV2 domainStored {22U, 60U};
    seedRecord(
        coreRig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &coreStored,
        sizeof(coreStored),
        1U
    );
    seedRecord(
        domainRig.backend,
        SLOT_A_KEY,
        2U,
        &domainStored,
        sizeof(domainStored),
        7U
    );
    TEST_ASSERT_TRUE(coreRig.begin());
    TEST_ASSERT_TRUE(domainRig.begin());
    ConfigLifecycle coreLifecycle(
        coreRig.storagePlan(),
        coreRig.adapterPlan(),
        coreRig.workspace()
    );
    ConfigLifecycle domainLifecycle(
        domainRig.storagePlan(),
        domainRig.adapterPlan(),
        domainRig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(coreLifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Migrated),
        static_cast<uint8_t>(domainLifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(11U, coreRig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(
        22U,
        domainRig.adapterContext.active.level
    );
    TEST_ASSERT_EQUAL_UINT32(0U, coreRig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(1U, domainRig.backend.writeCount);
}

void test_empty_and_corrupt_defaults_have_distinct_status() {
    TestRig emptyRig;
    TestRig corruptRig;
    const ExampleConfig stored = config(22U);
    seedRecord(
        corruptRig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &stored,
        sizeof(stored),
        2U
    );
    corruptRig.backend.raw(SLOT_A_KEY).data[
        Record::HEADER_SIZE
    ] ^= 0x55U;
    TEST_ASSERT_TRUE(emptyRig.begin());
    TEST_ASSERT_TRUE(corruptRig.begin());
    ConfigLifecycleOptions recoveryOptions {};
    recoveryOptions.allowDefaultsForCorruptStorage = true;
    ConfigLifecycle emptyLifecycle(
        emptyRig.storagePlan(),
        emptyRig.adapterPlan(),
        emptyRig.workspace()
    );
    ConfigLifecycle corruptLifecycle(
        corruptRig.storagePlan(),
        corruptRig.adapterPlan(),
        corruptRig.workspace(),
        recoveryOptions
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::DefaultsApplied),
        static_cast<uint8_t>(emptyLifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::RecoveryRequired),
        static_cast<uint8_t>(corruptLifecycle.startup())
    );
    TEST_ASSERT_TRUE(emptyLifecycle.status().hasStoredSchema);
    TEST_ASSERT_TRUE(emptyLifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(emptyLifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(emptyLifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(1U, emptyRig.backend.writeCount);
    TEST_ASSERT_FALSE(corruptLifecycle.status().hasStoredSchema);
    TEST_ASSERT_FALSE(corruptLifecycle.status().desiredActiveAligned);
    TEST_ASSERT_TRUE(corruptLifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(corruptLifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(0U, corruptRig.backend.writeCount);
}

void test_startup_structural_validation_failure_preserves_record() {
    TestRig rig;
    const ConfigV2 malformedCurrent {20U, 50U};
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &malformedCurrent,
        sizeof(malformedCurrent),
        3U
    );
    const size_t originalLength = rig.backend.raw(SLOT_A_KEY).length;
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ValidationFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_FALSE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(
        originalLength,
        rig.backend.raw(SLOT_A_KEY).length
    );
}

void test_startup_hardware_validation_failure_preserves_record() {
    TestRig rig;
    const ExampleConfig unsupported = config(20U, 120U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &unsupported,
        sizeof(unsupported),
        3U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ValidationFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT32(1U, rig.trace.count('H'));
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.applyCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.adapterContext.commitCount);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
    TEST_ASSERT_TRUE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
}

void test_migration_persist_failure_keeps_active_and_old_stored() {
    TestRig rig;
    const ConfigV1 stored {25U};
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        1U,
        &stored,
        sizeof(stored),
        9U
    );
    TEST_ASSERT_TRUE(rig.begin());
    rig.backend.failNextWrite = true;
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::PersistFailed),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_TRUE(lifecycle.status().hasActiveConfig);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigSource::Migrated),
        static_cast<uint8_t>(lifecycle.status().source)
    );
    TEST_ASSERT_EQUAL_UINT16(1U, lifecycle.status().storedSchemaVersion);
    TEST_ASSERT_EQUAL_UINT16(
        CURRENT_SCHEMA,
        lifecycle.status().activeSchemaVersion
    );
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_EQUAL_UINT32(25U, rig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.raw(SLOT_B_KEY).length);
}

void test_migration_rewrites_canonical_behind_newer_old_record() {
    TestRig rig;
    const ExampleConfig canonical = config(25U);
    const ConfigV1 newerOld {25U};
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &canonical,
        sizeof(canonical),
        8U
    );
    seedRecord(
        rig.backend,
        SLOT_B_KEY,
        1U,
        &newerOld,
        sizeof(newerOld),
        9U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Migrated),
        static_cast<uint8_t>(lifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::Success),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
    TEST_ASSERT_EQUAL_UINT32(1U, rig.backend.writeCount);
    const StorageRawRecord latest = rig.storage.loadLatestRaw(
        rig.rawBytes,
        sizeof(rig.rawBytes)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StorageRawLoadResult::Success),
        static_cast<uint8_t>(latest.result)
    );
    TEST_ASSERT_EQUAL_UINT16(CURRENT_SCHEMA, latest.schemaVersion);
    TEST_ASSERT_EQUAL_UINT32(10U, latest.generation);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StorageSlot::A),
        static_cast<uint8_t>(latest.slot)
    );
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
}

void test_no_change_retry_applies_persisted_desired_and_clears_recovery() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const ExampleConfig desired = config(35U);
    rig.adapterContext.failApply = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    const size_t writesAfterFailure = rig.backend.writeCount;
    const size_t commitsAfterFailure = rig.adapterContext.commitCount;
    rig.adapterContext.failApply = false;
    rig.trace = {};

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::NoChange),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(writesAfterFailure, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(
        commitsAfterFailure + 1U,
        rig.adapterContext.commitCount
    );
    TEST_ASSERT_EQUAL_UINT32(35U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::NoChange),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
    assertOrdered(rig.trace, 'P', 'R');
    assertOrdered(rig.trace, 'R', 'A');
    assertOrdered(rig.trace, 'A', 'C');
}

void test_restart_required_no_change_preserves_legal_divergence() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const ExampleConfig desired = config(40U, 50U, 1U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::RestartRequired),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    const size_t writesAfterFirst = rig.backend.writeCount;
    const size_t appliesAfterFirst = rig.adapterContext.applyCount;
    const size_t commitsAfterFirst = rig.adapterContext.commitCount;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::RestartRequired),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::NoChange),
        static_cast<uint8_t>(
            lifecycle.status().lastPersistenceResult
        )
    );
    TEST_ASSERT_EQUAL_UINT32(writesAfterFirst, rig.backend.writeCount);
    TEST_ASSERT_EQUAL_UINT32(
        appliesAfterFirst,
        rig.adapterContext.applyCount
    );
    TEST_ASSERT_EQUAL_UINT32(
        commitsAfterFirst,
        rig.adapterContext.commitCount
    );
    TEST_ASSERT_EQUAL_UINT32(10U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
}

void test_persist_failure_then_success_clears_degraded_status() {
    TestRig rig;
    const ExampleConfig initial = config(10U);
    seedRecord(
        rig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &initial,
        sizeof(initial),
        1U
    );
    TEST_ASSERT_TRUE(rig.begin());
    ConfigLifecycle lifecycle(
        rig.storagePlan(),
        rig.adapterPlan(),
        rig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(lifecycle.startup())
    );
    const ExampleConfig desired = config(20U);
    rig.backend.failNextWrite = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::PersistFailed),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    TEST_ASSERT_TRUE(lifecycle.status().degraded);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(
            lifecycle.update(&desired, sizeof(desired))
        )
    );
    TEST_ASSERT_EQUAL_UINT32(20U, rig.adapterContext.active.level);
    TEST_ASSERT_TRUE(lifecycle.status().desiredActiveAligned);
    TEST_ASSERT_FALSE(lifecycle.status().restartRequired);
    TEST_ASSERT_FALSE(lifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(lifecycle.status().degraded);
}

void test_recovery_failure_isolated_between_lifecycles() {
    TestRig coreRig;
    TestRig domainRig;
    const ExampleConfig coreStored = config(11U);
    const ExampleConfig domainStored = config(22U);
    seedRecord(
        coreRig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &coreStored,
        sizeof(coreStored),
        1U
    );
    seedRecord(
        domainRig.backend,
        SLOT_A_KEY,
        CURRENT_SCHEMA,
        &domainStored,
        sizeof(domainStored),
        1U
    );
    TEST_ASSERT_TRUE(coreRig.begin());
    TEST_ASSERT_TRUE(domainRig.begin());
    ConfigLifecycle coreLifecycle(
        coreRig.storagePlan(),
        coreRig.adapterPlan(),
        coreRig.workspace()
    );
    ConfigLifecycle domainLifecycle(
        domainRig.storagePlan(),
        domainRig.adapterPlan(),
        domainRig.workspace()
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(coreLifecycle.startup())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::Success),
        static_cast<uint8_t>(domainLifecycle.startup())
    );
    const ConfigLifecycleStatus domainBefore = domainLifecycle.status();
    coreRig.adapterContext.failApply = true;
    const ExampleConfig coreDesired = config(30U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigOperationResult::ApplyFailed),
        static_cast<uint8_t>(
            coreLifecycle.update(&coreDesired, sizeof(coreDesired))
        )
    );
    TEST_ASSERT_TRUE(coreLifecycle.status().recoveryRequired);
    TEST_ASSERT_TRUE(coreLifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(domainBefore.lastResult),
        static_cast<uint8_t>(domainLifecycle.status().lastResult)
    );
    TEST_ASSERT_EQUAL_UINT32(
        domainBefore.desiredActiveAligned,
        domainLifecycle.status().desiredActiveAligned
    );
    TEST_ASSERT_FALSE(domainLifecycle.status().recoveryRequired);
    TEST_ASSERT_FALSE(domainLifecycle.status().degraded);
    TEST_ASSERT_EQUAL_UINT32(22U, domainRig.adapterContext.active.level);
    TEST_ASSERT_EQUAL_UINT32(0U, domainRig.backend.writeCount);
}

void test_storage_adapter_rejects_schema_or_size_mismatch() {
    TestRig rig;
    TEST_ASSERT_TRUE(rig.begin());
    const ConfigStorage storage = rig.storagePlan();
    const ExampleConfig candidate = config(17U);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigPersistenceResult::InvalidArgument
        ),
        static_cast<uint8_t>(
            storage.persist(
                storage.context,
                CURRENT_SCHEMA + 1U,
                &candidate,
                sizeof(candidate)
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigPersistenceResult::InvalidArgument
        ),
        static_cast<uint8_t>(
            storage.persist(
                storage.context,
                CURRENT_SCHEMA,
                &candidate,
                sizeof(candidate) - 1U
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT32(0U, rig.backend.writeCount);
}

void test_storage_result_mapping_preserves_no_change_and_failures() {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::Success),
        static_cast<uint8_t>(
            mapStoragePersistenceResult(
                StorageOperationResult::Success
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::NoChange),
        static_cast<uint8_t>(
            mapStoragePersistenceResult(
                StorageOperationResult::NoChange
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigPersistenceResult::ValidationFailure
        ),
        static_cast<uint8_t>(
            mapStoragePersistenceResult(
                StorageOperationResult::ValidationFailure
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ConfigPersistenceResult::BackendFailure
        ),
        static_cast<uint8_t>(
            mapStoragePersistenceResult(
                StorageOperationResult::BackendFailure
            )
        )
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ConfigPersistenceResult::VerifyFailure),
        static_cast<uint8_t>(
            mapStoragePersistenceResult(
                StorageOperationResult::VerifyFailure
            )
        )
    );
}

} // namespace

void setUp() {
}

void tearDown() {
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_startup_current_loads_applies_without_rewrite);
    RUN_TEST(test_startup_empty_applies_defaults_then_persists);
    RUN_TEST(test_startup_empty_persist_failure_keeps_active_defaults);
    RUN_TEST(test_startup_old_schema_migrates_stepwise_then_persists);
    RUN_TEST(test_startup_migration_failure_does_not_apply_or_write);
    RUN_TEST(test_startup_migration_respects_maximum_steps);
    RUN_TEST(test_startup_future_schema_is_preserved_and_rejected);
    RUN_TEST(test_startup_semantic_validation_failure_preserves_record);
    RUN_TEST(test_startup_apply_failure_does_not_publish_or_rewrite);
    RUN_TEST(test_startup_corrupt_requires_recovery_without_fallback);
    RUN_TEST(test_startup_corrupt_uses_recovery_defaults_without_write);
    RUN_TEST(test_overlapping_workspace_is_rejected);
    RUN_TEST(test_insufficient_raw_workspace_capacity_is_rejected);
    RUN_TEST(test_runtime_live_change_persists_before_apply);
    RUN_TEST(test_runtime_identical_desired_maps_no_change_to_success);
    RUN_TEST(test_runtime_invalid_proposal_does_not_persist_or_apply);
    RUN_TEST(test_runtime_persist_failure_does_not_apply);
    RUN_TEST(test_runtime_apply_failure_keeps_old_active_and_desired);
    RUN_TEST(test_runtime_restart_required_persists_without_live_apply);
    RUN_TEST(test_next_startup_retries_persisted_desired);
    RUN_TEST(test_two_lifecycles_are_independent);
    RUN_TEST(test_empty_and_corrupt_defaults_have_distinct_status);
    RUN_TEST(test_startup_structural_validation_failure_preserves_record);
    RUN_TEST(test_startup_hardware_validation_failure_preserves_record);
    RUN_TEST(test_migration_persist_failure_keeps_active_and_old_stored);
    RUN_TEST(test_migration_rewrites_canonical_behind_newer_old_record);
    RUN_TEST(test_no_change_retry_applies_persisted_desired_and_clears_recovery);
    RUN_TEST(test_restart_required_no_change_preserves_legal_divergence);
    RUN_TEST(test_persist_failure_then_success_clears_degraded_status);
    RUN_TEST(test_recovery_failure_isolated_between_lifecycles);
    RUN_TEST(test_storage_adapter_rejects_schema_or_size_mismatch);
    RUN_TEST(test_storage_result_mapping_preserves_no_change_and_failures);

    return UNITY_END();
}
