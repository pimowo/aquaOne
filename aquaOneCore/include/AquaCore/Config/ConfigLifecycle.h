#pragma once

#include <stddef.h>
#include <stdint.h>

#include <AquaCore/Config/StorageService.h>

namespace AquaCore {
namespace Config {

enum class ConfigSource : uint8_t {
    None = 0U,
    Stored,
    Defaults,
    Migrated
};

enum class ConfigOperationResult : uint8_t {
    NotAttempted = 0U,
    Success,
    NoChange,
    DefaultsApplied,
    Migrated,
    RestartRequired,
    RecoveryRequired,
    InvalidArgument,
    LoadFailed,
    UnsupportedVersion,
    MigrationFailed,
    ValidationFailed,
    ApplyFailed,
    PersistFailed
};

enum class ConfigLoadResult : uint8_t {
    Success = 0U,
    Empty,
    Corrupt,
    Failure
};

enum class ConfigPersistenceResult : uint8_t {
    NotAttempted = 0U,
    Success,
    NoChange,
    InvalidArgument,
    ValidationFailure,
    BackendFailure,
    VerifyFailure
};

enum class ConfigApplyMode : uint8_t {
    Live = 0U,
    RestartRequired
};

struct ConfigLoadInfo {
    uint16_t schemaVersion = 0U;
    size_t payloadSize = 0U;
};

using ConfigLoadCallback = ConfigLoadResult (*)(
    void* context,
    void* rawPayload,
    size_t rawCapacity,
    ConfigLoadInfo& info
);

using ConfigPersistCallback = ConfigPersistenceResult (*)(
    void* context,
    uint16_t schemaVersion,
    const void* rawPayload,
    size_t payloadSize
);

struct ConfigStorage {
    void* context = nullptr;
    ConfigLoadCallback load = nullptr;
    ConfigPersistCallback persist = nullptr;
};

using ConfigBuildCallback = bool (*)(
    void* context,
    void* output,
    size_t outputCapacity,
    size_t& outputSize
);

using ConfigDecodeCallback = bool (*)(
    void* context,
    uint16_t schemaVersion,
    const void* rawPayload,
    size_t rawSize,
    void* output,
    size_t outputCapacity,
    size_t& outputSize
);

using ConfigMigrateCallback = bool (*)(
    void* context,
    uint16_t fromSchemaVersion,
    const void* input,
    size_t inputSize,
    void* output,
    size_t outputCapacity,
    size_t& outputSize
);

using ConfigValidateCallback = bool (*)(
    void* context,
    const void* candidate,
    size_t candidateSize
);

using ConfigEncodeCallback = bool (*)(
    void* context,
    const void* candidate,
    size_t candidateSize,
    void* rawOutput,
    size_t rawCapacity,
    size_t& rawSize
);

using ConfigApplyCallback = bool (*)(
    void* context,
    const void* candidate,
    size_t candidateSize
);

using ConfigCommitActiveCallback = void (*)(
    void* context,
    const void* candidate,
    size_t candidateSize
);

using ConfigClassifyCallback = ConfigApplyMode (*)(
    void* context,
    const void* candidate,
    size_t candidateSize
);

struct ConfigAdapter {
    void* context = nullptr;
    uint16_t currentSchemaVersion = 0U;
    uint16_t maximumMigrationSteps = 0U;
    ConfigBuildCallback buildDefaults = nullptr;
    ConfigDecodeCallback decode = nullptr;
    ConfigMigrateCallback migrateStep = nullptr;
    ConfigValidateCallback validateCurrent = nullptr;
    ConfigValidateCallback validateSemantic = nullptr;
    ConfigValidateCallback validateHardware = nullptr;
    ConfigEncodeCallback encodeCurrent = nullptr;
    ConfigApplyCallback apply = nullptr;
    ConfigCommitActiveCallback commitActive = nullptr;
    ConfigClassifyCallback classify = nullptr;
};

struct ConfigBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0U;
};

struct ConfigWorkspace {
    ConfigBuffer raw;
    ConfigBuffer candidateA;
    ConfigBuffer candidateB;
};

struct ConfigLifecycleOptions {
    bool allowDefaultsForCorruptStorage = false;
};

struct ConfigLifecycleStatus {
    ConfigSource source = ConfigSource::None;
    bool hasStoredSchema = false;
    uint16_t storedSchemaVersion = 0U;
    bool hasActiveConfig = false;
    uint16_t activeSchemaVersion = 0U;
    bool desiredActiveAligned = false;
    bool restartRequired = false;
    bool recoveryRequired = false;
    bool degraded = false;
    ConfigOperationResult lastResult =
        ConfigOperationResult::NotAttempted;
    ConfigPersistenceResult lastPersistenceResult =
        ConfigPersistenceResult::NotAttempted;
};

class ConfigLifecycle {
public:
    ConfigLifecycle(
        ConfigStorage storage,
        ConfigAdapter adapter,
        ConfigWorkspace workspace,
        ConfigLifecycleOptions options = {}
    );

    ConfigLifecycle(const ConfigLifecycle&) = delete;
    ConfigLifecycle& operator=(const ConfigLifecycle&) = delete;

    ConfigOperationResult startup();
    ConfigOperationResult update(
        const void* rawProposal,
        size_t proposalSize
    );
    ConfigLifecycleStatus status() const;

private:
    ConfigStorage storage_;
    ConfigAdapter adapter_;
    ConfigWorkspace workspace_;
    ConfigLifecycleOptions options_;
    ConfigLifecycleStatus status_;

    bool validPlan() const;
    bool validBufferResult(size_t size, const ConfigBuffer& buffer) const;
    bool decodeToCandidate(
        uint16_t schemaVersion,
        const void* raw,
        size_t rawSize,
        void*& candidate,
        size_t& candidateSize,
        bool& migrated
    );
    bool validateCandidate(const void* candidate, size_t candidateSize);
    bool encodeCandidate(
        const void* candidate,
        size_t candidateSize,
        size_t& rawSize
    );
    ConfigPersistenceResult persistCandidate(
        const void* candidate,
        size_t candidateSize
    );
    ConfigOperationResult applyAndCommit(
        const void* candidate,
        size_t candidateSize,
        ConfigSource source
    );
    ConfigOperationResult useDefaults(bool persistAfterApply);
    ConfigOperationResult fail(
        ConfigOperationResult result,
        bool recoveryRequired
    );
};

ConfigStorage storageServiceConfigStorage(StorageService& storage);
ConfigPersistenceResult mapStoragePersistenceResult(
    StorageOperationResult result
);

} // namespace Config
} // namespace AquaCore
