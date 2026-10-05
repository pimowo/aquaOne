#pragma once

#include <cstring>
#include <type_traits>

#include "AquaCore/Web/SnapshotSynchronizer.h"

namespace AquaCore {
namespace Web {

// Application is the single logical writer; concurrent readers copy values.
// T must be a bounded, self-contained value: no pointers, borrowed views,
// owning dynamic containers, transport handles, or Domain references.
// Trivial copyability is enforced; the remaining semantic restrictions belong
// to the projection type supplied by Application.
template <typename T>
class PublishedSnapshot {
    static_assert(std::is_trivially_copyable<T>::value,
                  "Web snapshot must be trivially copyable");
    static_assert(!std::is_pointer<T>::value,
                  "Web snapshot must be a self-contained value");

public:
    explicit PublishedSnapshot(SnapshotSynchronizer& synchronizer)
        : synchronizer_(synchronizer) {}
    PublishedSnapshot(const PublishedSnapshot&) = delete;
    PublishedSnapshot& operator=(const PublishedSnapshot&) = delete;

    // The caller supplies an already complete, validated projection.
    // False means locking failed; the publication was not changed.
    bool publish(const T& value) {
        if (!synchronizer_.lock()) {
            return false;
        }
        std::memcpy(&value_, &value, sizeof(T));
        available_ = true;
        synchronizer_.unlock();
        return true;
    }

    // Success gives the caller its own complete copy. False means unavailable
    // or lock failure and leaves out unchanged. No lock is held after return,
    // so serialization and socket writes must use only this copy.
    bool read(T& out) const {
        if (!synchronizer_.lock()) {
            return false;
        }
        if (!available_) {
            synchronizer_.unlock();
            return false;
        }
        std::memcpy(&out, &value_, sizeof(T));
        synchronizer_.unlock();
        return true;
    }

    // Removes availability without publishing a stale value. False means
    // locking failed and the previous publication remains unchanged.
    bool invalidate() {
        if (!synchronizer_.lock()) {
            return false;
        }
        available_ = false;
        synchronizer_.unlock();
        return true;
    }

private:
    SnapshotSynchronizer& synchronizer_;
    T value_ {};
    bool available_ = false;
};

} // namespace Web
} // namespace AquaCore
