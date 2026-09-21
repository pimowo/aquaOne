#include "AquaCore/Config/StorageService.h"

#include <cstring>
#include <limits.h>

#include "AquaCore/Config/Crc32.h"
#include "AquaCore/Config/StorageRecord.h"

namespace AquaCore {
namespace Config {

StorageService::StorageService(
    StorageBackend& backend,
    const char* storageNamespace,
    const char* slotAKey,
    const char* slotBKey,
    StorageWorkspace workspace
)
    : backend_(backend),
      storageNamespace_(storageNamespace),
      slotAKey_(slotAKey),
      slotBKey_(slotBKey),
      recordBuffer_(workspace.buffer),
      recordCapacity_(workspace.capacity) {
}

StorageService::~StorageService() {
    if (opened_) {
        backend_.end();
    }
}

bool StorageService::begin(
    size_t payloadSize,
    uint16_t schemaVersion,
    PayloadValidator validator
) {
    if (!configure(payloadSize, schemaVersion, validator)) {
        hasValidPayload_ = false;
        activeSlot_ = NO_SLOT;
        activeGeneration_ = 0U;
        return false;
    }

    if (!opened_) {
        if (!backend_.begin(storageNamespace_)) {
            hasValidPayload_ = false;
            activeSlot_ = NO_SLOT;
            activeGeneration_ = 0U;
            return false;
        }

        opened_ = true;
    }

    refreshStatus();
    return true;
}

bool StorageService::load(void* payload) {
    if (!opened_ || payload == nullptr) {
        lastLoadResult_ = StorageOperationResult::Failure;
        return false;
    }

    uint8_t selectedSlot = NO_SLOT;
    SlotInfo selectedInfo {};

    if (!selectLatest(selectedSlot, selectedInfo)) {
        hasValidPayload_ = false;
        activeSlot_ = NO_SLOT;
        activeGeneration_ = 0U;
        lastLoadResult_ = StorageOperationResult::Failure;
        return false;
    }

    SlotInfo verifiedInfo {};
    if (!readSlot(selectedSlot, verifiedInfo, payload)) {
        refreshStatus();
        lastLoadResult_ = StorageOperationResult::Failure;
        return false;
    }

    hasValidPayload_ = true;
    activeSlot_ = selectedSlot;
    activeGeneration_ = verifiedInfo.generation;
    lastLoadResult_ = StorageOperationResult::Success;
    return true;
}

StorageRawRecord StorageService::loadLatestRaw(
    void* payload,
    size_t payloadCapacity
) {
    StorageRawRecord result {};
    rawBaseValid_ = false;
    rawBaseSlot_ = NO_SLOT;
    rawBaseGeneration_ = 0U;

    if (!opened_ || payload == nullptr) {
        result.result = StorageRawLoadResult::InvalidArgument;
        return result;
    }

    RawSlotInfo slotAInfo {};
    RawSlotInfo slotBInfo {};
    const bool slotAValid = readRawSlot(SLOT_A, slotAInfo);
    const bool slotBValid = readRawSlot(SLOT_B, slotBInfo);

    if (!slotAValid && !slotBValid) {
        result.result =
            !slotAInfo.present && !slotBInfo.present
                ? StorageRawLoadResult::Empty
                : StorageRawLoadResult::NoValidRecord;
        return result;
    }

    uint8_t selectedSlot = SLOT_A;
    RawSlotInfo selectedInfo = slotAInfo;
    if (
        slotBValid &&
        (
            !slotAValid ||
            isGenerationNewer(
                slotBInfo.generation,
                slotAInfo.generation
            )
        )
    ) {
        selectedSlot = SLOT_B;
        selectedInfo = slotBInfo;
    }

    if (payloadCapacity < selectedInfo.payloadSize) {
        result.result = StorageRawLoadResult::InsufficientCapacity;
        return result;
    }

    RawSlotInfo verifiedInfo {};
    if (!readRawSlot(selectedSlot, verifiedInfo)) {
        result.result = StorageRawLoadResult::NoValidRecord;
        return result;
    }

    std::memcpy(
        payload,
        recordBuffer_ + StorageRecord::HEADER_SIZE,
        verifiedInfo.payloadSize
    );

    rawBaseValid_ = true;
    rawBaseSlot_ = selectedSlot;
    rawBaseGeneration_ = verifiedInfo.generation;
    result.result = StorageRawLoadResult::Success;
    result.schemaVersion = verifiedInfo.schemaVersion;
    result.payloadSize = verifiedInfo.payloadSize;
    result.generation = verifiedInfo.generation;
    result.slot =
        selectedSlot == SLOT_A
            ? StorageSlot::A
            : StorageSlot::B;
    return result;
}

bool StorageService::saveCurrentRaw(
    uint16_t schemaVersion,
    const void* payload,
    size_t payloadSize
) {
    if (
        schemaVersion != schemaVersion_ ||
        payloadSize != payloadSize_
    ) {
        lastSaveResult_ = StorageOperationResult::InvalidArgument;
        return false;
    }

    rawBaseForSave_ = rawBaseValid_;
    const bool saved = save(payload);
    rawBaseForSave_ = false;
    return saved;
}

bool StorageService::save(const void* payload) {
    if (
        !opened_ ||
        payload == nullptr ||
        payloadOverlapsWorkspace(payload)
    ) {
        lastSaveResult_ = StorageOperationResult::InvalidArgument;
        return false;
    }

    if (!validator_(payload, payloadSize_)) {
        lastSaveResult_ = StorageOperationResult::ValidationFailure;
        return false;
    }

    uint8_t currentSlot = NO_SLOT;
    SlotInfo currentInfo {};
    const bool currentExists = selectLatest(currentSlot, currentInfo);
    const bool currentCoversRawBase =
        !rawBaseForSave_ ||
        (
            currentSlot == rawBaseSlot_ &&
            currentInfo.generation == rawBaseGeneration_
        ) ||
        isGenerationNewer(
            currentInfo.generation,
            rawBaseGeneration_
        );

    if (currentExists && currentCoversRawBase) {
        SlotInfo comparedInfo {};
        if (
            readSlot(currentSlot, comparedInfo, nullptr) &&
            comparedInfo.generation == currentInfo.generation &&
            std::memcmp(
                recordBuffer_ + StorageRecord::HEADER_SIZE,
                payload,
                payloadSize_
            ) == 0
        ) {
            hasValidPayload_ = true;
            activeSlot_ = currentSlot;
            activeGeneration_ = currentInfo.generation;
            rawBaseValid_ = true;
            rawBaseSlot_ = currentSlot;
            rawBaseGeneration_ = currentInfo.generation;
            lastSaveResult_ = StorageOperationResult::NoChange;
            return true;
        }
    }

    const bool useCurrentBase =
        currentExists && currentCoversRawBase;
    const bool baseExists =
        useCurrentBase || (rawBaseForSave_ && rawBaseValid_);
    const uint8_t baseSlot =
        useCurrentBase ? currentSlot : rawBaseSlot_;
    const uint32_t baseGeneration =
        useCurrentBase
            ? currentInfo.generation
            : rawBaseGeneration_;
    const uint8_t targetSlot =
        !baseExists || baseSlot == SLOT_B
            ? SLOT_A
            : SLOT_B;
    const uint32_t nextGeneration =
        baseExists
            ? baseGeneration + 1U
            : 1U;

    std::memset(recordBuffer_, 0, recordSize_);
    writeUint32(
        recordBuffer_ + StorageRecord::MAGIC_OFFSET,
        StorageRecord::MAGIC
    );
    writeUint16(
        recordBuffer_ + StorageRecord::FORMAT_VERSION_OFFSET,
        StorageRecord::FORMAT_VERSION
    );
    writeUint16(
        recordBuffer_ + StorageRecord::SCHEMA_VERSION_OFFSET,
        schemaVersion_
    );
    writeUint32(
        recordBuffer_ + StorageRecord::PAYLOAD_LENGTH_OFFSET,
        static_cast<uint32_t>(payloadSize_)
    );
    writeUint32(
        recordBuffer_ + StorageRecord::GENERATION_OFFSET,
        nextGeneration
    );

    std::memcpy(
        recordBuffer_ + StorageRecord::HEADER_SIZE,
        payload,
        payloadSize_
    );
    writeUint32(
        recordBuffer_ + StorageRecord::PAYLOAD_CRC_OFFSET,
        crc32(
            recordBuffer_ + StorageRecord::HEADER_SIZE,
            payloadSize_
        )
    );
    writeUint32(
        recordBuffer_ + StorageRecord::HEADER_CRC_OFFSET,
        crc32(
            recordBuffer_,
            StorageRecord::HEADER_CRC_INPUT_SIZE
        )
    );

    const size_t written = backend_.writeBlob(
        slotKey(targetSlot),
        recordBuffer_,
        recordSize_
    );
    if (written != recordSize_) {
        refreshStatus();
        lastSaveResult_ = StorageOperationResult::BackendFailure;
        return false;
    }

    SlotInfo verifiedInfo {};
    if (
        !readSlot(targetSlot, verifiedInfo, nullptr) ||
        verifiedInfo.generation != nextGeneration ||
        std::memcmp(
            recordBuffer_ + StorageRecord::HEADER_SIZE,
            payload,
            payloadSize_
        ) != 0
    ) {
        refreshStatus();
        lastSaveResult_ = StorageOperationResult::VerifyFailure;
        return false;
    }

    hasValidPayload_ = true;
    activeSlot_ = targetSlot;
    activeGeneration_ = nextGeneration;
    rawBaseValid_ = true;
    rawBaseSlot_ = targetSlot;
    rawBaseGeneration_ = nextGeneration;
    lastSaveResult_ = StorageOperationResult::Success;
    return true;
}

bool StorageService::hasValidPayload() const {
    return hasValidPayload_;
}

StorageStatus StorageService::status() const {
    StorageStatus result {};
    result.backendReady = opened_;
    result.hasValidPayload = hasValidPayload_;
    result.activeGeneration = activeGeneration_;
    result.lastLoadResult = lastLoadResult_;
    result.lastSaveResult = lastSaveResult_;

    if (activeSlot_ == SLOT_A) {
        result.activeSlot = StorageSlot::A;
    } else if (activeSlot_ == SLOT_B) {
        result.activeSlot = StorageSlot::B;
    }

    return result;
}

bool StorageService::configure(
    size_t payloadSize,
    uint16_t schemaVersion,
    PayloadValidator validator
) {
    if (
        payloadSize == 0U ||
        payloadSize > UINT32_MAX ||
        payloadSize > SIZE_MAX - StorageRecord::HEADER_SIZE ||
        validator == nullptr ||
        storageNamespace_ == nullptr ||
        storageNamespace_[0] == '\0' ||
        slotAKey_ == nullptr ||
        slotAKey_[0] == '\0' ||
        slotBKey_ == nullptr ||
        slotBKey_[0] == '\0'
    ) {
        return false;
    }

    const size_t requestedRecordSize =
        StorageRecord::HEADER_SIZE + payloadSize;

    if (
        recordBuffer_ == nullptr ||
        recordCapacity_ < requestedRecordSize
    ) {
        return false;
    }

    payloadSize_ = payloadSize;
    recordSize_ = requestedRecordSize;
    schemaVersion_ = schemaVersion;
    validator_ = validator;
    return true;
}

const char* StorageService::slotKey(uint8_t slot) const {
    return slot == SLOT_A ? slotAKey_ : slotBKey_;
}

bool StorageService::readSlot(
    uint8_t slot,
    SlotInfo& info,
    void* payload
) {
    info = {};

    if (
        !opened_ ||
        recordBuffer_ == nullptr ||
        (slot != SLOT_A && slot != SLOT_B)
    ) {
        return false;
    }

    const char* key = slotKey(slot);
    if (backend_.blobLength(key) != recordSize_) {
        return false;
    }

    if (
        backend_.readBlob(key, recordBuffer_, recordSize_) !=
        recordSize_
    ) {
        return false;
    }

    if (
        readUint32(recordBuffer_ + StorageRecord::MAGIC_OFFSET) !=
            StorageRecord::MAGIC ||
        readUint16(
            recordBuffer_ + StorageRecord::FORMAT_VERSION_OFFSET
        ) != StorageRecord::FORMAT_VERSION ||
        readUint16(
            recordBuffer_ + StorageRecord::SCHEMA_VERSION_OFFSET
        ) != schemaVersion_ ||
        readUint32(
            recordBuffer_ + StorageRecord::PAYLOAD_LENGTH_OFFSET
        ) != payloadSize_
    ) {
        return false;
    }

    const uint32_t storedHeaderCrc = readUint32(
        recordBuffer_ + StorageRecord::HEADER_CRC_OFFSET
    );
    if (
        crc32(
            recordBuffer_,
            StorageRecord::HEADER_CRC_INPUT_SIZE
        ) != storedHeaderCrc
    ) {
        return false;
    }

    const uint32_t storedPayloadCrc = readUint32(
        recordBuffer_ + StorageRecord::PAYLOAD_CRC_OFFSET
    );
    if (
        crc32(
            recordBuffer_ + StorageRecord::HEADER_SIZE,
            payloadSize_
        ) != storedPayloadCrc
    ) {
        return false;
    }

    uint8_t* const candidate =
        recordBuffer_ + StorageRecord::HEADER_SIZE;
    if (!validator_(candidate, payloadSize_)) {
        return false;
    }

    info.valid = true;
    info.generation = readUint32(
        recordBuffer_ + StorageRecord::GENERATION_OFFSET
    );

    if (payload != nullptr) {
        std::memcpy(payload, candidate, payloadSize_);
    }

    return true;
}

bool StorageService::readRawSlot(
    uint8_t slot,
    RawSlotInfo& info
) {
    info = {};

    if (
        !opened_ ||
        recordBuffer_ == nullptr ||
        (slot != SLOT_A && slot != SLOT_B)
    ) {
        return false;
    }

    const char* key = slotKey(slot);
    const size_t storedSize = backend_.blobLength(key);
    if (storedSize == 0U) {
        return false;
    }

    info.present = true;
    if (
        storedSize < StorageRecord::HEADER_SIZE ||
        storedSize > recordCapacity_ ||
        backend_.readBlob(key, recordBuffer_, storedSize) != storedSize
    ) {
        return false;
    }

    if (
        readUint32(recordBuffer_ + StorageRecord::MAGIC_OFFSET) !=
            StorageRecord::MAGIC ||
        readUint16(
            recordBuffer_ + StorageRecord::FORMAT_VERSION_OFFSET
        ) != StorageRecord::FORMAT_VERSION
    ) {
        return false;
    }

    const uint32_t storedHeaderCrc = readUint32(
        recordBuffer_ + StorageRecord::HEADER_CRC_OFFSET
    );
    if (
        crc32(
            recordBuffer_,
            StorageRecord::HEADER_CRC_INPUT_SIZE
        ) != storedHeaderCrc
    ) {
        return false;
    }

    const uint32_t encodedPayloadSize = readUint32(
        recordBuffer_ + StorageRecord::PAYLOAD_LENGTH_OFFSET
    );
    if (
        encodedPayloadSize > SIZE_MAX - StorageRecord::HEADER_SIZE ||
        StorageRecord::HEADER_SIZE +
            static_cast<size_t>(encodedPayloadSize) != storedSize
    ) {
        return false;
    }

    const size_t payloadSize =
        static_cast<size_t>(encodedPayloadSize);
    const uint32_t storedPayloadCrc = readUint32(
        recordBuffer_ + StorageRecord::PAYLOAD_CRC_OFFSET
    );
    if (
        crc32(
            recordBuffer_ + StorageRecord::HEADER_SIZE,
            payloadSize
        ) != storedPayloadCrc
    ) {
        return false;
    }

    info.valid = true;
    info.schemaVersion = readUint16(
        recordBuffer_ + StorageRecord::SCHEMA_VERSION_OFFSET
    );
    info.payloadSize = payloadSize;
    info.generation = readUint32(
        recordBuffer_ + StorageRecord::GENERATION_OFFSET
    );
    return true;
}

bool StorageService::selectLatest(
    uint8_t& slot,
    SlotInfo& info
) {
    SlotInfo slotAInfo {};
    SlotInfo slotBInfo {};
    const bool slotAValid = readSlot(SLOT_A, slotAInfo, nullptr);
    const bool slotBValid = readSlot(SLOT_B, slotBInfo, nullptr);

    if (!slotAValid && !slotBValid) {
        slot = NO_SLOT;
        info = {};
        return false;
    }

    if (
        slotBValid &&
        (
            !slotAValid ||
            isGenerationNewer(
                slotBInfo.generation,
                slotAInfo.generation
            )
        )
    ) {
        slot = SLOT_B;
        info = slotBInfo;
        return true;
    }

    slot = SLOT_A;
    info = slotAInfo;
    return true;
}

bool StorageService::payloadOverlapsWorkspace(const void* payload) const {
    const uintptr_t payloadAddress = reinterpret_cast<uintptr_t>(payload);
    const uintptr_t workspaceAddress =
        reinterpret_cast<uintptr_t>(recordBuffer_);

    if (payloadAddress >= workspaceAddress) {
        return payloadAddress - workspaceAddress < recordSize_;
    }

    return workspaceAddress - payloadAddress < payloadSize_;
}

void StorageService::refreshStatus() {
    SlotInfo info {};
    if (!selectLatest(activeSlot_, info)) {
        hasValidPayload_ = false;
        activeSlot_ = NO_SLOT;
        activeGeneration_ = 0U;
        return;
    }

    hasValidPayload_ = true;
    activeGeneration_ = info.generation;
}

bool StorageService::isGenerationNewer(
    uint32_t candidate,
    uint32_t reference
) {
    const uint32_t difference = candidate - reference;
    return difference != 0U && difference < 0x80000000UL;
}

uint16_t StorageService::readUint16(const uint8_t* data) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8U)
    );
}

uint32_t StorageService::readUint32(const uint8_t* data) {
    return
        static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8U) |
        (static_cast<uint32_t>(data[2]) << 16U) |
        (static_cast<uint32_t>(data[3]) << 24U);
}

void StorageService::writeUint16(
    uint8_t* data,
    uint16_t value
) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8U);
}

void StorageService::writeUint32(
    uint8_t* data,
    uint32_t value
) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8U);
    data[2] = static_cast<uint8_t>(value >> 16U);
    data[3] = static_cast<uint8_t>(value >> 24U);
}

} // namespace Config
} // namespace AquaCore
