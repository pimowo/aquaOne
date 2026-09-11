#pragma once

#include "StorageBackend.h"

namespace AquaCore {
namespace Config {

// PreferencesType is injected so firmware and tests use the same adapter.
template <typename PreferencesType>
class PreferencesStorageBackend final : public StorageBackend {
public:
    bool begin(const char* storageNamespace) override {
        return preferences_.begin(storageNamespace, false);
    }

    void end() override {
        preferences_.end();
    }

    size_t blobLength(const char* key) override {
        return preferences_.getBytesLength(key);
    }

    size_t readBlob(
        const char* key,
        void* output,
        size_t maximumLength
    ) override {
        return preferences_.getBytes(key, output, maximumLength);
    }

    size_t writeBlob(
        const char* key,
        const void* input,
        size_t length
    ) override {
        return preferences_.putBytes(key, input, length);
    }

private:
    PreferencesType preferences_;
};

} // namespace Config
} // namespace AquaCore
