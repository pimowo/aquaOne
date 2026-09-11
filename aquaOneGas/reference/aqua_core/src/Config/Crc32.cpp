#include "AquaCore/Config/Crc32.h"

namespace AquaCore {
namespace Config {

uint32_t crc32(const void* data, size_t length) {
    if (data == nullptr && length != 0U) {
        return 0U;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t index = 0U; index < length; ++index) {
        crc ^= static_cast<uint32_t>(bytes[index]);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

} // namespace Config
} // namespace AquaCore
