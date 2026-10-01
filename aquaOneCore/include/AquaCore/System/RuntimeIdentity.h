#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AquaCore {
namespace Identity {

// Identifies one runtime instance, not a device or a security principal.
class RuntimeIdentity {
public:
    static constexpr size_t TEXT_CAPACITY = 17U;

    RuntimeIdentity() : value_(0U) {}

    // Zero is invalid. On failure, output becomes unavailable.
    static bool fromValue(uint64_t value, RuntimeIdentity& output);
    bool isValid() const { return value_ != 0U; }
    uint64_t value() const { return value_; }
    bool equals(const RuntimeIdentity& other) const { return value_ == other.value_; }
    bool operator==(const RuntimeIdentity& other) const { return equals(other); }
    bool operator!=(const RuntimeIdentity& other) const { return !equals(other); }

    // Writes exactly 16 uppercase hex digits and NUL. Invalid identity,
    // null output, or insufficient capacity fails without changing output.
    bool format(char* output, size_t capacity) const;

private:
    uint64_t value_;
};

class RuntimeIdentityGenerator {
public:
    // True requires a legal nonzero result; callers validate defensively.
    virtual bool generate(RuntimeIdentity& output) = 0;

protected:
    // Borrowed capability; never destroy a generator through this interface.
    ~RuntimeIdentityGenerator() = default;
};

// One authoritative value per ApplicationRuntime, owned by Composition Root.
// Construct a fresh state for each new runtime instance; readers receive copies.
class RuntimeIdentityState {
public:
    RuntimeIdentityState() : identity_(), attempted_(false) {}
    RuntimeIdentityState(const RuntimeIdentityState&) = delete;
    RuntimeIdentityState& operator=(const RuntimeIdentityState&) = delete;

    // One generator invocation at most, including when generation fails.
    bool initialize(RuntimeIdentityGenerator& generator);
    bool hasAttempted() const { return attempted_; }
    RuntimeIdentity identity() const { return identity_; }

private:
    RuntimeIdentity identity_;
    bool attempted_;
};

} // namespace Identity
} // namespace AquaCore
