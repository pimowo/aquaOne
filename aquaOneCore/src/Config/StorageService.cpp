#include "AquaCore/Config/StorageService.h"

#include <cstring>
#include <limits.h>
#include <new>

#include "AquaCore/Config/Crc32.h"
#include "AquaCore/Config/StorageRecord.h"

namespace AquaCore {
namespace Config {

StorageService::StorageService(
    StorageBackend& backend,
    const char* storageNamespace,
    const char* slotAKey,
    const char* slotBKey
)
    : backend_(backend),
      storageNamespace_(storageNamespace),
      slotAKey_(slotAKey),
      slotBKey_(slotBKey) {
}

StorageService::~StorageService() {
    if (opened_) {
        backend_.end();
    }

    delete[] recordBuffer_;
    delete[] candidateBuffer_;
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

bool StorageService::save(const void* payload) {
    if (
        !opened_ ||
        payload == nullptr ||
        !validator_(payload, payloadSize_)
    ) {
        lastSaveResult_ = StorageOperationResult::Failure;
        return false;
    }

    uint8_t currentSlot = NO_SLOT;
    SlotInfo currentInfo {};
    const bool currentExists = selectLatest(currentSlot, currentInfo);

    const uint8_t targetSlot =
        !currentExists || currentSlot == SLOT_B
            ? SLOT_A
            : SLOT_B;
    const uint32_t nextGeneration =
        currentExists
            ? currentInfo.generation + 1U
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
        lastSaveResult_ = StorageOperationResult::Failure;
        return false;
    }

    SlotInfo verifiedInfo {};
    if (
        !readSlot(targetSlot, verifiedInfo, nullptr) ||
        verifiedInfo.generation != nextGeneration ||
        std::memcmp(candidateBuffer_, payload, payloadSize_) != 0
    ) {
        refreshStatus();
        lastSaveResult_ = StorageOperationResult::Failure;
        return false;
    }

    hasValidPayload_ = true;
    activeSlot_ = targetSlot;
    activeGeneration_ = nextGeneration;
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
        recordBuffer_ != nullptr &&
        candidateBuffer_ != nullptr &&
        payloadSize_ == payloadSize
    ) {
        schemaVersion_ = schemaVersion;
        validator_ = validator;
        recordSize_ = requestedRecordSize;
        return true;
    }

    uint8_t* newRecord =
        new (std::nothrow) uint8_t[requestedRecordSize];
    uint8_t* newCandidate =
        new (std::nothrow) uint8_t[payloadSize];

    if (newRecord == nullptr || newCandidate == nullptr) {
        delete[] newRecord;
        delete[] newCandidate;
        return false;
    }

    delete[] recordBuffer_;
    delete[] candidateBuffer_;
    recordBuffer_ = newRecord;
    candidateBuffer_ = newCandidate;
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
        candidateBuffer_ == nullptr ||
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

    std::memcpy(
        candidateBuffer_,
        recordBuffer_ + StorageRecord::HEADER_SIZE,
        payloadSize_
    );
    if (!validator_(candidateBuffer_, payloadSize_)) {
        return false;
    }

    info.valid = true;
    info.generation = readUint32(
        recordBuffer_ + StorageRecord::GENERATION_OFFSET
    );

    if (payload != nullptr) {
        std::memcpy(payload, candidateBuffer_, payloadSize_);
    }

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
