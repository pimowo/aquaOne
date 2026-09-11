#include "TransitionEngine.h"

#include <algorithm>

#include "../../include/Constants.h"

namespace LumaSense {

void TransitionEngine::start(
    const ChannelLevels& from,
    const ChannelLevels& to,
    uint32_t durationMs,
    uint32_t nowMs
) {
    from_ = from;
    to_ = to;
    current_ = from;

    startMs_ = nowMs;
    durationMs_ = durationMs;

    if (durationMs_ == 0) {
        current_ = to_;
        active_ = false;
        return;
    }

    active_ = true;
}

ChannelLevels TransitionEngine::update(uint32_t nowMs) {
    if (!active_) {
        return current_;
    }

    const uint32_t elapsedMs = nowMs - startMs_;

    if (elapsedMs >= durationMs_) {
        current_ = to_;
        active_ = false;
        return current_;
    }

    float t =
        static_cast<float>(elapsedMs) /
        static_cast<float>(durationMs_);

    t = std::clamp(t, 0.0f, 1.0f);

    const float s = smoothstep(t);

    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        current_.value[channel] =
            from_.value[channel] +
            (to_.value[channel] - from_.value[channel]) * s;
    }

    return current_;
}

ChannelLevels TransitionEngine::update(
    uint32_t nowMs,
    const ChannelLevels& target
) {
    if (active_) {
        to_ = target;
    }

    return update(nowMs);
}

bool TransitionEngine::isActive() const {
    return active_;
}

void TransitionEngine::cancel() {
    active_ = false;
}

float TransitionEngine::smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

} // namespace LumaSense