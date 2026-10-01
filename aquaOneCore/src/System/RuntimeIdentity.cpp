#include "AquaCore/System/RuntimeIdentity.h"

namespace AquaCore {
namespace Identity {

bool RuntimeIdentity::fromValue(uint64_t value, RuntimeIdentity& output) {
    output.value_ = value;
    return output.isValid();
}

bool RuntimeIdentity::format(char* output, size_t capacity) const {
    if (!isValid() || output == nullptr || capacity < TEXT_CAPACITY) {
        return false;
    }

    static const char digits[] = "0123456789ABCDEF";
    for (size_t index = 0U; index < 16U; ++index) {
        const unsigned shift = static_cast<unsigned>((15U - index) * 4U);
        output[index] = digits[(value_ >> shift) & 0xFU];
    }
    output[16] = '\0';
    return true;
}

bool RuntimeIdentityState::initialize(RuntimeIdentityGenerator& generator) {
    if (attempted_) {
        return identity_.isValid();
    }

    attempted_ = true;
    RuntimeIdentity candidate;
    if (!generator.generate(candidate) || !candidate.isValid()) {
        return false;
    }
    identity_ = candidate;
    return true;
}

} // namespace Identity
} // namespace AquaCore
