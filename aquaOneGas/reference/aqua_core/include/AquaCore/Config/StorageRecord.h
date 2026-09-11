#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Config {
namespace StorageRecord {

constexpr uint32_t MAGIC = 0x4C534346UL;
constexpr uint16_t FORMAT_VERSION = 1U;
constexpr size_t MAGIC_OFFSET = 0U;
constexpr size_t FORMAT_VERSION_OFFSET = 4U;
constexpr size_t SCHEMA_VERSION_OFFSET = 6U;
constexpr size_t PAYLOAD_LENGTH_OFFSET = 8U;
constexpr size_t GENERATION_OFFSET = 12U;
constexpr size_t PAYLOAD_CRC_OFFSET = 16U;
constexpr size_t HEADER_CRC_OFFSET = 20U;
constexpr size_t HEADER_CRC_INPUT_SIZE = 20U;
constexpr size_t HEADER_SIZE = 24U;

} // namespace StorageRecord
} // namespace Config
} // namespace AquaCore
