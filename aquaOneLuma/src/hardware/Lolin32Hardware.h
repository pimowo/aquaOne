#pragma once

#include <stdint.h>

#include "HardwareInterface.h"

namespace LumaSense {

class Lolin32Hardware : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override;

    bool setChannelPercent(
        uint8_t channel,
        float percent
    ) override;

    bool allChannelsOff() override;

    bool isReady() const override;

    uint8_t availableChannelCount() const override;

    bool isChannelAvailable(
        uint8_t channel
    ) const override;

private:
    // Test GPIOs for LOLIN32 Lite.
    // GPIO21/22 remain reserved for DS3231.
    static constexpr uint8_t PWM_PINS[8] = {
        23, // CH1
        19, // CH2
        18, // CH3
        17, // CH4
        16, // CH5
        25, // CH6
        26, // CH7
        27  // CH8
    };

    static constexpr uint8_t PWM_CHANNELS[8] = {
        0, 1, 2, 3, 4, 5, 6, 7
    };

    bool pwmInverted_[8] {};
    bool attached_[8] {};
    bool ready_ = false;

    uint32_t percentToPwm(
        float percent
    ) const;

    uint32_t physicalPwm(
        uint8_t channel,
        float percent
    ) const;

    bool writePhysicalPwm(uint8_t channel, uint32_t pwm);

    bool stopPwmAtPhysicalOff(uint8_t channel);

    bool setPinToSafeGpioOff(uint8_t channel);
};

} // namespace LumaSense