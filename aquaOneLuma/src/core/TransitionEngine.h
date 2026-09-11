#pragma once

#include <stdint.h>

#include "../profiles/ProfileTypes.h"

namespace LumaSense {

class TransitionEngine {
public:
    void start(
        const ChannelLevels& from,
        const ChannelLevels& to,
        uint32_t durationMs,
        uint32_t nowMs
    );

    ChannelLevels update(uint32_t nowMs);

    ChannelLevels update(
        uint32_t nowMs,
        const ChannelLevels& target
    );

    bool isActive() const;

    void cancel();

private:
    static float smoothstep(float t);

    ChannelLevels from_ {};
    ChannelLevels to_ {};
    ChannelLevels current_ {};

    uint32_t startMs_ = 0;
    uint32_t durationMs_ = 0;

    bool active_ = false;
};

} // namespace LumaSense