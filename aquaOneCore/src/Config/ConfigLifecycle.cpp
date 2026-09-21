#include <AquaCore/Config/ConfigLifecycle.h>

#include <stdint.h>

namespace AquaCore {
namespace Config {
namespace {

ConfigLoadResult loadFromStorageService(
    void* context,
    void* rawPayload,
    size_t rawCapacity,
    ConfigLoadInfo& info
) {
    if (context == nullptr) {
        return ConfigLoadResult::Failure;
    }

    StorageService& storage =
        *static_cast<StorageService*>(context);
    const StorageRawRecord record =
        storage.loadLatestRaw(rawPayload, rawCapacity);

    if (record.result == StorageRawLoadResult::Success) {
        info.schemaVersion = record.schemaVersion;
        info.payloadSize = record.payloadSize;
        return ConfigLoadResult::Success;
    }
    if (record.result == StorageRawLoadResult::Empty) {
        return ConfigLoadResult::Empty;
    }
    if (record.result == StorageRawLoadResult::NoValidRecord) {
        return ConfigLoadResult::Corrupt;
    }
    return ConfigLoadResult::Failure;
}

ConfigPersistenceResult persistToStorageService(
    void* context,
    uint16_t schemaVersion,
    const void* rawPayload,
    size_t payloadSize
) {
    if (context == nullptr) {
        return ConfigPersistenceResult::InvalidArgument;
    }

    StorageService& storage =
        *static_cast<StorageService*>(context);
    storage.saveCurrentRaw(schemaVersion, rawPayload, payloadSize);
    return mapStoragePersistenceResult(
        storage.status().lastSaveResult
    );
}

bool rangesOverlap(const ConfigBuffer& left, const ConfigBuffer& right) {
    const uintptr_t leftAddress =
        reinterpret_cast<uintptr_t>(left.data);
    const uintptr_t rightAddress =
        reinterpret_cast<uintptr_t>(right.data);

    if (leftAddress >= rightAddress) {
        return leftAddress - rightAddress < right.capacity;
    }
    return rightAddress - leftAddress < left.capacity;
}

bool persistenceSucceeded(ConfigPersistenceResult result) {
    return
        result == ConfigPersistenceResult::Success ||
        result == ConfigPersistenceResult::NoChange;
}

} // namespace

ConfigLifecycle::ConfigLifecycle(
    ConfigStorage storage,
    ConfigAdapter adapter,
    ConfigWorkspace workspace,
    ConfigLifecycleOptions options
)
    : storage_(storage),
      adapter_(adapter),
      workspace_(workspace),
      options_(options) {
}

bool ConfigLifecycle::validPlan() const {
    if (
        storage_.load == nullptr ||
        storage_.persist == nullptr ||
        adapter_.buildDefaults == nullptr ||
        adapter_.decode == nullptr ||
        adapter_.validateCurrent == nullptr ||
        adapter_.validateSemantic == nullptr ||
        adapter_.encodeCurrent == nullptr ||
        adapter_.apply == nullptr ||
        adapter_.commitActive == nullptr
    ) {
        return false;
    }

    const ConfigBuffer buffers[] = {
        workspace_.raw,
        workspace_.candidateA,
        workspace_.candidateB
    };
    for (const ConfigBuffer& buffer : buffers) {
        if (buffer.data == nullptr || buffer.capacity == 0U) {
            return false;
        }
    }

    return
        !rangesOverlap(workspace_.raw, workspace_.candidateA) &&
        !rangesOverlap(workspace_.raw, workspace_.candidateB) &&
        !rangesOverlap(workspace_.candidateA, workspace_.candidateB);
}

bool ConfigLifecycle::validBufferResult(
    size_t size,
    const ConfigBuffer& buffer
) const {
    return size > 0U && size <= buffer.capacity;
}

ConfigLifecycleStatus ConfigLifecycle::status() const {
    return status_;
}

ConfigOperationResult ConfigLifecycle::fail(
    ConfigOperationResult result,
    bool recoveryRequired
) {
    status_.lastResult = result;
    if (recoveryRequired) {
        status_.recoveryRequired = true;
        status_.degraded = true;
    }
    return result;
}

ConfigPersistenceResult mapStoragePersistenceResult(
    StorageOperationResult result
) {
    switch (result) {
        case StorageOperationResult::Success:
            return ConfigPersistenceResult::Success;
        case StorageOperationResult::NoChange:
            return ConfigPersistenceResult::NoChange;
        case StorageOperationResult::InvalidArgument:
            return ConfigPersistenceResult::InvalidArgument;
        case StorageOperationResult::ValidationFailure:
            return ConfigPersistenceResult::ValidationFailure;
        case StorageOperationResult::BackendFailure:
            return ConfigPersistenceResult::BackendFailure;
        case StorageOperationResult::VerifyFailure:
            return ConfigPersistenceResult::VerifyFailure;
        default:
            return ConfigPersistenceResult::BackendFailure;
    }
}

ConfigStorage storageServiceConfigStorage(StorageService& storage) {
    ConfigStorage result {};
    result.context = &storage;
    result.load = loadFromStorageService;
    result.persist = persistToStorageService;
    return result;
}

bool ConfigLifecycle::decodeToCandidate(
    uint16_t schemaVersion,
    const void* raw,
    size_t rawSize,
    void*& candidate,
    size_t& candidateSize,
    bool& migrated
) {
    candidate = workspace_.candidateA.data;
    candidateSize = 0U;
    migrated = false;

    if (
        !adapter_.decode(
            adapter_.context,
            schemaVersion,
            raw,
            rawSize,
            candidate,
            workspace_.candidateA.capacity,
            candidateSize
        ) ||
        !validBufferResult(candidateSize, workspace_.candidateA)
    ) {
        return false;
    }

    uint16_t version = schemaVersion;
    uint16_t steps = 0U;
    ConfigBuffer currentBuffer = workspace_.candidateA;
    ConfigBuffer nextBuffer = workspace_.candidateB;

    while (version < adapter_.currentSchemaVersion) {
        if (
            adapter_.migrateStep == nullptr ||
            steps >= adapter_.maximumMigrationSteps
        ) {
            return false;
        }

        size_t nextSize = 0U;
        if (
            !adapter_.migrateStep(
                adapter_.context,
                version,
                currentBuffer.data,
                candidateSize,
                nextBuffer.data,
                nextBuffer.capacity,
                nextSize
            ) ||
            !validBufferResult(nextSize, nextBuffer)
        ) {
            return false;
        }

        candidate = nextBuffer.data;
        candidateSize = nextSize;
        const ConfigBuffer swap = currentBuffer;
        currentBuffer = nextBuffer;
        nextBuffer = swap;
        ++version;
        ++steps;
        migrated = true;
    }

    return version == adapter_.currentSchemaVersion;
}

bool ConfigLifecycle::validateCandidate(
    const void* candidate,
    size_t candidateSize
) {
    return
        adapter_.validateCurrent(
            adapter_.context,
            candidate,
            candidateSize
        ) &&
        adapter_.validateSemantic(
            adapter_.context,
            candidate,
            candidateSize
        ) &&
        (
            adapter_.validateHardware == nullptr ||
            adapter_.validateHardware(
                adapter_.context,
                candidate,
                candidateSize
            )
        );
}

bool ConfigLifecycle::encodeCandidate(
    const void* candidate,
    size_t candidateSize,
    size_t& rawSize
) {
    rawSize = 0U;
    return
        adapter_.encodeCurrent(
            adapter_.context,
            candidate,
            candidateSize,
            workspace_.raw.data,
            workspace_.raw.capacity,
            rawSize
        ) &&
        validBufferResult(rawSize, workspace_.raw);
}

ConfigPersistenceResult ConfigLifecycle::persistCandidate(
    const void* candidate,
    size_t candidateSize
) {
    size_t rawSize = 0U;
    if (!encodeCandidate(candidate, candidateSize, rawSize)) {
        return ConfigPersistenceResult::InvalidArgument;
    }

    return storage_.persist(
        storage_.context,
        adapter_.currentSchemaVersion,
        workspace_.raw.data,
        rawSize
    );
}

ConfigOperationResult ConfigLifecycle::applyAndCommit(
    const void* candidate,
    size_t candidateSize,
    ConfigSource source
) {
    if (
        !adapter_.apply(
            adapter_.context,
            candidate,
            candidateSize
        )
    ) {
        return fail(ConfigOperationResult::ApplyFailed, true);
    }

    adapter_.commitActive(
        adapter_.context,
        candidate,
        candidateSize
    );
    status_.source = source;
    status_.hasActiveConfig = true;
    status_.activeSchemaVersion = adapter_.currentSchemaVersion;
    return ConfigOperationResult::Success;
}

ConfigOperationResult ConfigLifecycle::useDefaults(
    bool persistAfterApply
) {
    size_t candidateSize = 0U;
    if (
        !adapter_.buildDefaults(
            adapter_.context,
            workspace_.candidateA.data,
            workspace_.candidateA.capacity,
            candidateSize
        ) ||
        !validBufferResult(candidateSize, workspace_.candidateA)
    ) {
        return fail(ConfigOperationResult::InvalidArgument, true);
    }

    if (
        !validateCandidate(
            workspace_.candidateA.data,
            candidateSize
        )
    ) {
        return fail(ConfigOperationResult::ValidationFailed, true);
    }

    if (
        applyAndCommit(
            workspace_.candidateA.data,
            candidateSize,
            ConfigSource::Defaults
        ) == ConfigOperationResult::ApplyFailed
    ) {
        return ConfigOperationResult::ApplyFailed;
    }

    if (!persistAfterApply) {
        status_.desiredActiveAligned = false;
        status_.recoveryRequired = true;
        status_.degraded = true;
        status_.lastResult = ConfigOperationResult::RecoveryRequired;
        return status_.lastResult;
    }

    status_.lastPersistenceResult = persistCandidate(
        workspace_.candidateA.data,
        candidateSize
    );
    if (!persistenceSucceeded(status_.lastPersistenceResult)) {
        status_.desiredActiveAligned = false;
        status_.degraded = true;
        status_.lastResult = ConfigOperationResult::PersistFailed;
        return status_.lastResult;
    }

    status_.hasStoredSchema = true;
    status_.storedSchemaVersion = adapter_.currentSchemaVersion;
    status_.desiredActiveAligned = true;
    status_.lastResult = ConfigOperationResult::DefaultsApplied;
    return status_.lastResult;
}

ConfigOperationResult ConfigLifecycle::startup() {
    status_ = {};
    if (!validPlan()) {
        return fail(ConfigOperationResult::InvalidArgument, false);
    }

    ConfigLoadInfo loadInfo {};
    const ConfigLoadResult loadResult = storage_.load(
        storage_.context,
        workspace_.raw.data,
        workspace_.raw.capacity,
        loadInfo
    );

    if (loadResult == ConfigLoadResult::Empty) {
        return useDefaults(true);
    }
    if (loadResult == ConfigLoadResult::Corrupt) {
        if (options_.allowDefaultsForCorruptStorage) {
            return useDefaults(false);
        }
        return fail(ConfigOperationResult::RecoveryRequired, true);
    }
    if (loadResult != ConfigLoadResult::Success) {
        return fail(ConfigOperationResult::LoadFailed, true);
    }
    if (
        loadInfo.payloadSize == 0U ||
        loadInfo.payloadSize > workspace_.raw.capacity
    ) {
        return fail(ConfigOperationResult::LoadFailed, true);
    }

    status_.hasStoredSchema = true;
    status_.storedSchemaVersion = loadInfo.schemaVersion;
    if (loadInfo.schemaVersion > adapter_.currentSchemaVersion) {
        return fail(
            ConfigOperationResult::UnsupportedVersion,
            true
        );
    }

    void* candidate = nullptr;
    size_t candidateSize = 0U;
    bool migrated = false;
    if (
        !decodeToCandidate(
            loadInfo.schemaVersion,
            workspace_.raw.data,
            loadInfo.payloadSize,
            candidate,
            candidateSize,
            migrated
        )
    ) {
        return fail(
            loadInfo.schemaVersion < adapter_.currentSchemaVersion
                ? ConfigOperationResult::MigrationFailed
                : ConfigOperationResult::ValidationFailed,
            true
        );
    }

    if (!validateCandidate(candidate, candidateSize)) {
        return fail(ConfigOperationResult::ValidationFailed, true);
    }

    if (
        applyAndCommit(
            candidate,
            candidateSize,
            migrated ? ConfigSource::Migrated : ConfigSource::Stored
        ) == ConfigOperationResult::ApplyFailed
    ) {
        return ConfigOperationResult::ApplyFailed;
    }

    if (!migrated) {
        status_.desiredActiveAligned = true;
        status_.lastResult = ConfigOperationResult::Success;
        return status_.lastResult;
    }

    status_.lastPersistenceResult =
        persistCandidate(candidate, candidateSize);
    if (!persistenceSucceeded(status_.lastPersistenceResult)) {
        status_.desiredActiveAligned = false;
        status_.degraded = true;
        status_.lastResult = ConfigOperationResult::PersistFailed;
        return status_.lastResult;
    }

    status_.storedSchemaVersion = adapter_.currentSchemaVersion;
    status_.desiredActiveAligned = true;
    status_.lastResult = ConfigOperationResult::Migrated;
    return status_.lastResult;
}

ConfigOperationResult ConfigLifecycle::update(
    const void* rawProposal,
    size_t proposalSize
) {
    if (
        !validPlan() ||
        !status_.hasActiveConfig ||
        rawProposal == nullptr ||
        proposalSize == 0U
    ) {
        return fail(ConfigOperationResult::InvalidArgument, false);
    }

    status_.lastPersistenceResult =
        ConfigPersistenceResult::NotAttempted;

    void* candidate = workspace_.candidateA.data;
    size_t candidateSize = 0U;
    if (
        !adapter_.decode(
            adapter_.context,
            adapter_.currentSchemaVersion,
            rawProposal,
            proposalSize,
            candidate,
            workspace_.candidateA.capacity,
            candidateSize
        ) ||
        !validBufferResult(candidateSize, workspace_.candidateA) ||
        !validateCandidate(candidate, candidateSize)
    ) {
        status_.lastResult = ConfigOperationResult::ValidationFailed;
        return status_.lastResult;
    }

    status_.lastPersistenceResult =
        persistCandidate(candidate, candidateSize);
    if (!persistenceSucceeded(status_.lastPersistenceResult)) {
        status_.degraded = true;
        status_.lastResult = ConfigOperationResult::PersistFailed;
        return status_.lastResult;
    }

    const ConfigApplyMode mode =
        adapter_.classify == nullptr
            ? ConfigApplyMode::Live
            : adapter_.classify(
                adapter_.context,
                candidate,
                candidateSize
            );

    status_.hasStoredSchema = true;
    status_.storedSchemaVersion = adapter_.currentSchemaVersion;
    if (mode == ConfigApplyMode::RestartRequired) {
        status_.desiredActiveAligned = false;
        status_.restartRequired = true;
        status_.recoveryRequired = false;
        status_.degraded = false;
        status_.lastResult = ConfigOperationResult::RestartRequired;
        return status_.lastResult;
    }

    if (
        !adapter_.apply(
            adapter_.context,
            candidate,
            candidateSize
        )
    ) {
        status_.desiredActiveAligned = false;
        status_.restartRequired = false;
        status_.recoveryRequired = true;
        status_.degraded = true;
        status_.lastResult = ConfigOperationResult::ApplyFailed;
        return status_.lastResult;
    }

    adapter_.commitActive(
        adapter_.context,
        candidate,
        candidateSize
    );
    status_.source = ConfigSource::Stored;
    status_.hasActiveConfig = true;
    status_.activeSchemaVersion = adapter_.currentSchemaVersion;
    status_.desiredActiveAligned = true;
    status_.restartRequired = false;
    status_.recoveryRequired = false;
    status_.degraded = false;
    status_.lastResult =
        status_.lastPersistenceResult ==
            ConfigPersistenceResult::NoChange
            ? ConfigOperationResult::NoChange
            : ConfigOperationResult::Success;
    return status_.lastResult;
}

} // namespace Config
} // namespace AquaCore
