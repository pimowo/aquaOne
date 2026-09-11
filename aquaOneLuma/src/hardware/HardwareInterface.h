#pragma once

#include <stdint.h>

namespace LumaSense {

struct ChannelConfig;

class HardwareInterface {
public:
    virtual ~HardwareInterface() = default;

    virtual bool begin(const ChannelConfig* channels) = 0;

    virtual bool setChannelPercent(uint8_t channel, float percent) = 0;

    virtual bool allChannelsOff() = 0;

    virtual bool isReady() const = 0;

    virtual uint8_t availableChannelCount() const = 0;

    virtual bool isChannelAvailable(uint8_t channel) const = 0;
};

} // namespace LumaSense