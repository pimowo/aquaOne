#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Preferences {
public:
    bool begin(
        const char*,
        bool = false,
        const char* = nullptr
    ) {
        opened_ = beginResult_;
        return opened_;
    }

    void end() {
        opened_ = false;
    }

    size_t getBytesLength(
        const char* key
    ) {
        if (!opened_) {
            return 0U;
        }

        return slot(key).length;
    }

    size_t getBytes(
        const char* key,
        void* output,
        size_t maximumLength
    ) {
        if (!opened_ || output == nullptr) {
            return 0U;
        }

        const Slot& selected =
            slot(key);

        if (
            selected.length == 0U ||
            selected.length > maximumLength
        ) {
            return 0U;
        }

        memcpy(
            output,
            selected.data,
            selected.length
        );

        return selected.length;
    }

    size_t putBytes(
        const char* key,
        const void* input,
        size_t length
    ) {
        if (
            !opened_ ||
            input == nullptr ||
            length > SLOT_CAPACITY
        ) {
            return 0U;
        }

        if (failNextWrite_) {
            failNextWrite_ = false;
            return 0U;
        }

        Slot& selected =
            slot(key);

        if (partialWriteArmed_) {
            size_t partialLength =
                partialWriteLength_;

            if (partialLength > length) {
                partialLength = length;
            }

            memcpy(
                selected.data,
                input,
                partialLength
            );

            selected.length =
                partialLength;

            partialWriteArmed_ = false;

            return partialLength;
        }

        memcpy(
            selected.data,
            input,
            length
        );

        selected.length = length;

        if (
            corruptAfterWriteArmed_ &&
            corruptAfterWriteOffset_ < length
        ) {
            selected.data[
                corruptAfterWriteOffset_
            ] ^= 0x5AU;
        }

        corruptAfterWriteArmed_ = false;

        return length;
    }

    static void reset() {
        memset(
            slots_,
            0,
            sizeof(slots_)
        );

        beginResult_ = true;
        failNextWrite_ = false;
        partialWriteArmed_ = false;
        partialWriteLength_ = 0U;
        corruptAfterWriteArmed_ = false;
        corruptAfterWriteOffset_ = 0U;
    }

    static void setBeginResult(bool result) {
        beginResult_ = result;
    }

    static void failNextWrite() {
        failNextWrite_ = true;
    }

    static void partialNextWrite(
        size_t length
    ) {
        partialWriteArmed_ = true;
        partialWriteLength_ = length;
    }

    static void corruptAfterNextWrite(
        size_t offset
    ) {
        corruptAfterWriteArmed_ = true;
        corruptAfterWriteOffset_ = offset;
    }

    static uint8_t* raw(
        const char* key
    ) {
        return slot(key).data;
    }

    static size_t length(
        const char* key
    ) {
        return slot(key).length;
    }

    static void setLength(
        const char* key,
        size_t length
    ) {
        slot(key).length =
            length <= SLOT_CAPACITY
                ? length
                : SLOT_CAPACITY;
    }

private:
    static constexpr size_t SLOT_CAPACITY = 8192U;

    struct Slot {
        uint8_t data[SLOT_CAPACITY];
        size_t length;
    };

    static Slot& slot(
        const char* key
    ) {
        return (
            key != nullptr &&
            strcmp(key, "cfg_b") == 0
        )
            ? slots_[1]
            : slots_[0];
    }

    bool opened_ = false;

    inline static Slot slots_[2] {};
    inline static bool beginResult_ = true;
    inline static bool failNextWrite_ = false;
    inline static bool partialWriteArmed_ = false;
    inline static size_t partialWriteLength_ = 0U;
    inline static bool corruptAfterWriteArmed_ = false;
    inline static size_t corruptAfterWriteOffset_ = 0U;
};