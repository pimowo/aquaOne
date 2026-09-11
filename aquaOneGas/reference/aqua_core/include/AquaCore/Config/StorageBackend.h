#pragma once

#include <stddef.h>

namespace AquaCore {
namespace Config {

class StorageBackend {
public:
    virtual ~StorageBackend() = default;
    virtual bool begin(const char* storageNamespace) = 0;
    virtual void end() = 0;
    virtual size_t blobLength(const char* key) = 0;
    virtual size_t readBlob(
        const char* key,
        void* output,
        size_t maximumLength
    ) = 0;
    virtual size_t writeBlob(
        const char* key,
        const void* input,
        size_t length
    ) = 0;
};

} // namespace Config
} // namespace AquaCore
