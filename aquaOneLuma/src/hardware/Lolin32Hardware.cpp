#include "Lolin32Hardware.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include "Constants.h"
#include "../storage/ConfigTypes.h"

namespace LumaSense {

constexpr uint8_t Lolin32Hardware::PWM_PINS[8];
constexpr uint8_t Lolin32Hardware::PWM_CHANNELS[8];

// =========================================================
// Hardware startup
// =========================================================

bool Lolin32Hardware::begin(
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
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        pwmInverted_[ch] =
            channels[ch].pwmInverted;

        attached_[ch] = false;
    }

    // -----------------------------------------------------
    // Put every pin into a safe physical OFF state
    // before LEDC is attached.
    // -----------------------------------------------------

    for (
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        if (!setPinToSafeGpioOff(ch)) {
            allChannelsOff();
            return false;
        }
    }

    // -----------------------------------------------------
    // Attach LEDC one channel at a time.
    //
    // GPIO hold keeps the pad at the physical OFF level
    // while the peripheral is being attached and configured.
    // -----------------------------------------------------

    for (
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        const uint8_t pin =
            PWM_PINS[ch];

        const gpio_num_t gpio =
            static_cast<gpio_num_t>(pin);

        if (gpio_hold_en(gpio) != ESP_OK) {
            allChannelsOff();
            return false;
        }

        const bool attached =
            ledcAttachChannel(
                pin,
                PWM_FREQUENCY_HZ,
                PWM_RESOLUTION_BITS,
                PWM_CHANNELS[ch]
            );

        if (!attached) {
            allChannelsOff();

            // The latch was already prepared for OFF.
            // Release the hold only after the failed
            // LEDC attachment has been handled.
            gpio_hold_dis(gpio);

            return false;
        }

        attached_[ch] = true;

        // Configure LEDC itself to physical OFF while
        // the GPIO pad is still held at OFF.
        if (
            !writePhysicalPwm(
                ch,
                physicalPwm(ch, 0.0f)
            )
        ) {
            ready_ = false;
            allChannelsOff();

            // If allChannelsOff could not completely
            // recover the output, keeping the GPIO held
            // is safer than releasing an unknown state.
            return false;
        }

        // LEDC now contains the correct physical OFF duty.
        // It is safe to connect it to the pad.
        if (gpio_hold_dis(gpio) != ESP_OK) {
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

bool Lolin32Hardware::setChannelPercent(
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

    const uint32_t pwm =
        physicalPwm(
            channel,
            percent
        );

    if (!writePhysicalPwm(channel, pwm)) {
        ready_ = false;

        allChannelsOff();

        return false;
    }

    return true;
}

// =========================================================
// All channels OFF
// =========================================================

bool Lolin32Hardware::allChannelsOff() {
    bool success = true;

    for (
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        // -------------------------------------------------
        // Normal case:
        // LEDC is attached and accepts the physical OFF duty.
        // -------------------------------------------------

        if (attached_[ch]) {
            const uint32_t offPwm =
                physicalPwm(
                    ch,
                    0.0f
                );

            if (
                writePhysicalPwm(
                    ch,
                    offPwm
                )
            ) {
                continue;
            }

            // ---------------------------------------------
            // LEDC write failed.
            //
            // Prepare the GPIO latch first, stop the LEDC
            // peripheral at the correct physical OFF level,
            // then try to detach it.
            // ---------------------------------------------

            success = false;

            const uint8_t pin =
                PWM_PINS[ch];

            const gpio_num_t gpio =
                static_cast<gpio_num_t>(
                    pin
                );

            const uint32_t offLevel =
                pwmInverted_[ch]
                    ? 1U
                    : 0U;

            const bool latchPrepared =
                gpio_set_level(
                    gpio,
                    offLevel
                ) == ESP_OK;

            const bool pwmStopped =
                stopPwmAtPhysicalOff(ch);

            const bool detached =
                ledcDetach(pin);

            if (detached) {
                attached_[ch] = false;
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
        // If LEDC is no longer attached, GPIO itself owns
        // the output and must hold physical OFF.
        // -------------------------------------------------

        if (!attached_[ch]) {
            if (!setPinToSafeGpioOff(ch)) {
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

bool Lolin32Hardware::isReady() const {
    return ready_;
}

// =========================================================
// Channel count
// =========================================================

uint8_t Lolin32Hardware::availableChannelCount() const {
    return CHANNEL_COUNT;
}

// =========================================================
// Channel availability
// =========================================================

bool Lolin32Hardware::isChannelAvailable(
    uint8_t channel
) const {
    return channel < CHANNEL_COUNT;
}

// =========================================================
// Percent -> logical PWM
// =========================================================

uint32_t Lolin32Hardware::percentToPwm(
    float percent
) const {
    if (percent <= 0.0f) {
        return 0;
    }

    if (percent >= 100.0f) {
        return PWM_MAX_VALUE;
    }

    const float normalized =
        percent / 100.0f;

    return static_cast<uint32_t>(
        normalized *
        static_cast<float>(
            PWM_MAX_VALUE
        ) +
        0.5f
    );
}

// =========================================================
// Logical PWM -> physical PWM
// =========================================================

uint32_t Lolin32Hardware::physicalPwm(
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

bool Lolin32Hardware::writePhysicalPwm(
    uint8_t channel,
    uint32_t pwm
) {
    if (
        channel >= CHANNEL_COUNT ||
        !attached_[channel]
    ) {
        return false;
    }

    // Arduino-ESP32 3.x ledcWrite() already returns
    // information about whether the operation succeeded.
    //
    // Do NOT use ledc_set_duty_and_update() here:
    // that API requires the LEDC fade service.
    return ledcWrite(
        PWM_PINS[channel],
        pwm
    );
}

// =========================================================
// Emergency LEDC stop
// =========================================================

bool Lolin32Hardware::stopPwmAtPhysicalOff(
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

bool Lolin32Hardware::setPinToSafeGpioOff(
    uint8_t channel
) {
    if (channel >= CHANNEL_COUNT) {
        return false;
    }

    const uint8_t pin =
        PWM_PINS[channel];

    const gpio_num_t gpio =
        static_cast<gpio_num_t>(pin);

    const uint32_t offLevel =
        pwmInverted_[channel]
            ? 1U
            : 0U;

    // Preload the GPIO output latch BEFORE enabling
    // the output driver. This minimizes the possibility
    // of a short pulse during initialization.
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