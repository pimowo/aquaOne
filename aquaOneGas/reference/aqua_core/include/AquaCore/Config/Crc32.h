#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Config {

uint32_t crc32(const void* data, size_t length);

} // namespace Config
} // namespace AquaCore
