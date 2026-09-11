#pragma once

#include <stddef.h>
#include <stdint.h>

#include "StorageBackend.h"

namespace AquaCore {
namespace Config {

using PayloadValidator = bool (*)(const void* payload, size_t payloadSize);

enum class StorageSlot : uint8_t {
    None = 0U,
    A,
    B
};

enum class StorageOperationResult : uint8_t {
    NotAttempted = 0U,
    Success,
    Failure
};

struct StorageStatus {
    bool backendReady = false;
    bool hasValidPayload = false;
    StorageSlot activeSlot = StorageSlot::None;
    uint32_t activeGeneration = 0U;
    StorageOperationResult lastLoadResult =
        StorageOperationResult::NotAttempted;
    StorageOperationResult lastSaveResult =
        StorageOperationResult::NotAttempted;
};

class StorageService {
public:
    StorageService(
        StorageBackend& backend,
        const char* storageNamespace,
        const char* slotAKey,
        const char* slotBKey
    );
    ~StorageService();

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    bool begin(
        size_t payloadSize,
        uint16_t schemaVersion,
        PayloadValidator validator
    );
    bool load(void* payload);
    bool save(const void* payload);
    bool hasValidPayload() const;
    StorageStatus status() const;

private:
    struct SlotInfo {
        bool valid = false;
        uint32_t generation = 0U;
    };

    static constexpr uint8_t SLOT_A = 0U;
    static constexpr uint8_t SLOT_B = 1U;
    static constexpr uint8_t NO_SLOT = 0xFFU;

    StorageBackend& backend_;
    const char* storageNamespace_;
    const char* slotAKey_;
    const char* slotBKey_;
    uint8_t* recordBuffer_ = nullptr;
    uint8_t* candidateBuffer_ = nullptr;
    size_t payloadSize_ = 0U;
    size_t recordSize_ = 0U;
    uint16_t schemaVersion_ = 0U;
    PayloadValidator validator_ = nullptr;
    bool opened_ = false;
    bool hasValidPayload_ = false;
    uint8_t activeSlot_ = NO_SLOT;
    uint32_t activeGeneration_ = 0U;
    StorageOperationResult lastLoadResult_ =
        StorageOperationResult::NotAttempted;
    StorageOperationResult lastSaveResult_ =
        StorageOperationResult::NotAttempted;

    bool configure(
        size_t payloadSize,
        uint16_t schemaVersion,
        PayloadValidator validator
    );
    const char* slotKey(uint8_t slot) const;
    bool readSlot(uint8_t slot, SlotInfo& info, void* payload);
    bool selectLatest(uint8_t& slot, SlotInfo& info);
    void refreshStatus();
    static bool isGenerationNewer(uint32_t candidate, uint32_t reference);
    static uint16_t readUint16(const uint8_t* data);
    static uint32_t readUint32(const uint8_t* data);
    static void writeUint16(uint8_t* data, uint16_t value);
    static void writeUint32(uint8_t* data, uint32_t value);
};

} // namespace Config
} // namespace AquaCore
