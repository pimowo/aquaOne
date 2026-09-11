#pragma once

#include <stdint.h>

#include "HardwareInterface.h"

namespace LumaSense {

class AqmaHardware : public HardwareInterface {
public:
    bool begin(const ChannelConfig* channels) override;

    bool setChannelPercent(uint8_t channel, float percent) override;

    bool allChannelsOff() override;

    bool isReady() const override;

    uint8_t availableChannelCount() const override;

    bool isChannelAvailable(uint8_t channel) const override;

private:
    static constexpr uint8_t PWM_PINS[8] = {
        23, // CH1
        19, // CH2
        17, // CH3
        16, // CH4
        26, // CH5
        25, // CH6
        27, // CH7
        13  // CH8
    };

    static constexpr uint8_t PWM_CHANNELS[8] = {
        0, // CH1
        1, // CH2
        2, // CH3
        3, // CH4
        4, // CH5
        5, // CH6
        6, // CH7
        7  // CH8
    };

    bool pwmInverted_[8] {};
    bool attached_[8] {};
    bool ready_ = false;

    uint32_t percentToPwm(float percent) const;

    uint32_t physicalPwm(uint8_t channel, float percent) const;

    bool writePhysicalPwm(uint8_t channel, uint32_t pwm);

    bool stopPwmAtPhysicalOff(uint8_t channel);

    bool setPinToSafeGpioOff(uint8_t channel);
};

} // namespace LumaSense