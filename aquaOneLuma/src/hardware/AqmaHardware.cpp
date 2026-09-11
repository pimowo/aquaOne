#include "AqmaHardware.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include "../../include/Constants.h"
#include "../storage/ConfigTypes.h"

namespace LumaSense {

constexpr uint8_t AqmaHardware::PWM_PINS[8];
constexpr uint8_t AqmaHardware::PWM_CHANNELS[8];

// =========================================================
// Hardware startup
// =========================================================

bool AqmaHardware::begin(
    const ChannelConfig* channels
) {
    ready_ = false;

    if (channels == nullptr) {
        return false;
    }

    // -----------------------------------------------------
    // Capture hardware polarity configuration.
    //
    // pwmInverted is a hardware setting.
    // Changing it requires a restart and another begin().
    // -----------------------------------------------------

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        pwmInverted_[channel] =
            channels[channel]
                .pwmInverted;

        attached_[channel] =
            false;
    }

    // -----------------------------------------------------
    // Put every PWM pin into physical OFF before LEDC.
    // -----------------------------------------------------

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        if (
            !setPinToSafeGpioOff(
                channel
            )
        ) {
            allChannelsOff();
            return false;
        }
    }

    // -----------------------------------------------------
    // Attach LEDC while GPIO pad remains held at OFF.
    // -----------------------------------------------------

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        const uint8_t pin =
            PWM_PINS[channel];

        const gpio_num_t gpio =
            static_cast<gpio_num_t>(
                pin
            );

        if (
            gpio_hold_en(gpio) !=
            ESP_OK
        ) {
            allChannelsOff();
            return false;
        }

        const bool attached =
            ledcAttachChannel(
                pin,
                PWM_FREQUENCY_HZ,
                PWM_RESOLUTION_BITS,
                PWM_CHANNELS[channel]
            );

        if (!attached) {
            allChannelsOff();

            gpio_hold_dis(gpio);

            return false;
        }

        attached_[channel] =
            true;

        if (
            !writePhysicalPwm(
                channel,
                physicalPwm(
                    channel,
                    0.0f
                )
            )
        ) {
            ready_ = false;

            allChannelsOff();

            return false;
        }

        if (
            gpio_hold_dis(gpio) !=
            ESP_OK
        ) {
            ready_ = false;

            allChannelsOff();

            return false;
        }
    }

    ready_ = true;

    return true;
}

// =========================================================
// Channel output
// =========================================================

bool AqmaHardware::setChannelPercent(
    uint8_t channel,
    float percent
) {
    if (
        !ready_ ||
        !isChannelAvailable(channel) ||
        !attached_[channel]
    ) {
        return false;
    }

    const uint32_t pwmValue =
        physicalPwm(
            channel,
            percent
        );

    if (
        !writePhysicalPwm(
            channel,
            pwmValue
        )
    ) {
        ready_ = false;

        allChannelsOff();

        return false;
    }

    return true;
}

// =========================================================
// All channels OFF
// =========================================================

bool AqmaHardware::allChannelsOff() {
    bool success = true;

    for (
        uint8_t channel = 0;
        channel < CHANNEL_COUNT;
        ++channel
    ) {
        const uint8_t pin =
            PWM_PINS[channel];

        // -------------------------------------------------
        // Normal OFF through LEDC.
        // -------------------------------------------------

        if (attached_[channel]) {
            const uint32_t offPwm =
                physicalPwm(
                    channel,
                    0.0f
                );

            if (
                writePhysicalPwm(
                    channel,
                    offPwm
                )
            ) {
                continue;
            }

            // ---------------------------------------------
            // LEDC write failed.
            // Prepare GPIO OFF, stop PWM at physical OFF
            // and attempt to detach the peripheral.
            // ---------------------------------------------

            success = false;

            const gpio_num_t gpio =
                static_cast<gpio_num_t>(
                    pin
                );

            const uint32_t offLevel =
                pwmInverted_[channel]
                    ? 1U
                    : 0U;

            const bool latchPrepared =
                gpio_set_level(
                    gpio,
                    offLevel
                ) == ESP_OK;

            const bool pwmStopped =
                stopPwmAtPhysicalOff(
                    channel
                );

            const bool detached =
                ledcDetach(pin);

            if (detached) {
                attached_[channel] =
                    false;
            }

            if (
                !latchPrepared ||
                !pwmStopped ||
                !detached
            ) {
                success = false;
            }
        }

        // -------------------------------------------------
        // GPIO fallback after successful LEDC detach.
        // -------------------------------------------------

        if (!attached_[channel]) {
            if (
                !setPinToSafeGpioOff(
                    channel
                )
            ) {
                success = false;
            }
        }
    }

    if (!success) {
        ready_ = false;
    }

    return success;
}

// =========================================================
// Hardware state
// =========================================================

bool AqmaHardware::isReady() const {
    return ready_;
}

// =========================================================
// Channel count
// =========================================================

uint8_t AqmaHardware::availableChannelCount() const {
    return CHANNEL_COUNT;
}

// =========================================================
// Channel availability
// =========================================================

bool AqmaHardware::isChannelAvailable(
    uint8_t channel
) const {
    return channel < CHANNEL_COUNT;
}

// =========================================================
// Percent -> logical PWM
// =========================================================

uint32_t AqmaHardware::percentToPwm(
    float percent
) const {
    if (percent <= 0.0f) {
        return 0;
    }

    if (percent >= 100.0f) {
        return PWM_MAX_VALUE;
    }

    return static_cast<uint32_t>(
        (percent / 100.0f) *
        static_cast<float>(
            PWM_MAX_VALUE
        ) +
        0.5f
    );
}

// =========================================================
// Logical PWM -> physical PWM
// =========================================================

uint32_t AqmaHardware::physicalPwm(
    uint8_t channel,
    float percent
) const {
    const uint32_t logicalPwm =
        percentToPwm(percent);

    return pwmInverted_[channel]
        ? PWM_MAX_VALUE - logicalPwm
        : logicalPwm;
}

// =========================================================
// PWM write
// =========================================================

bool AqmaHardware::writePhysicalPwm(
    uint8_t channel,
    uint32_t pwm
) {
    if (
        channel >= CHANNEL_COUNT ||
        !attached_[channel]
    ) {
        return false;
    }

    // No LEDC fade service is needed.
    // TransitionEngine performs the logical fades.
    return ledcWrite(
        PWM_PINS[channel],
        pwm
    );
}

// =========================================================
// Emergency LEDC stop
// =========================================================

bool AqmaHardware::stopPwmAtPhysicalOff(
    uint8_t channel
) {
    if (channel >= CHANNEL_COUNT) {
        return false;
    }

    const uint8_t ledcChannel =
        PWM_CHANNELS[channel];

    const ledc_mode_t speedMode =
        static_cast<ledc_mode_t>(
            ledcChannel / 8U
        );

    const ledc_channel_t driverChannel =
        static_cast<ledc_channel_t>(
            ledcChannel % 8U
        );

    const uint32_t offLevel =
        pwmInverted_[channel]
            ? 1U
            : 0U;

    return ledc_stop(
        speedMode,
        driverChannel,
        offLevel
    ) == ESP_OK;
}

// =========================================================
// Safe GPIO OFF
// =========================================================

bool AqmaHardware::setPinToSafeGpioOff(
    uint8_t channel
) {
    if (channel >= CHANNEL_COUNT) {
        return false;
    }

    const uint8_t pin =
        PWM_PINS[channel];

    const gpio_num_t gpio =
        static_cast<gpio_num_t>(
            pin
        );

    const uint32_t offLevel =
        pwmInverted_[channel]
            ? 1U
            : 0U;

    // Preload the output latch before enabling the pin
    // as an output.
    if (
        gpio_set_level(
            gpio,
            offLevel
        ) != ESP_OK
    ) {
        return false;
    }

    pinMode(
        pin,
        OUTPUT
    );

    return gpio_set_level(
        gpio,
        offLevel
    ) == ESP_OK;
}

} // namespace LumaSense