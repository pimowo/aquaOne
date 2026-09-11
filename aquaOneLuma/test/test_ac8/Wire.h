#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class TwoWire {
public:
    bool begin(int, int, uint32_t = 0U) {
        began_ = beginResult_;
        return beginResult_;
    }

    void beginTransmission(uint8_t address) {
        address_ = address;
        txLength_ = 0U;
    }

    size_t write(uint8_t value) {
        if (failNextWrite_) {
            failNextWrite_ = false;
            return 0U;
        }

        if (txLength_ >= sizeof(txBuffer_)) {
            return 0U;
        }

        txBuffer_[txLength_++] = value;
        return 1U;
    }

    uint8_t endTransmission(bool) {
        ++endTransmissionCalls_;

        if (
            failEndTransmissionCall_ > 0 &&
            endTransmissionCalls_ == failEndTransmissionCall_
        ) {
            return 4U;
        }

        if (address_ != 0x68U || txLength_ == 0U) {
            return 4U;
        }

        registerPointer_ = txBuffer_[0];

        if (!discardWrites_) {
            for (size_t i = 1U; i < txLength_; ++i) {
                registers_[registerPointer_++] = txBuffer_[i];
            }
        }

        if (txLength_ == 1U) {
            registerPointer_ = txBuffer_[0];
        }

        return 0U;
    }

    uint8_t endTransmission() {
        return endTransmission(true);
    }

    size_t requestFrom(uint8_t address, size_t length) {
        if (failNextRequest_) {
            failNextRequest_ = false;
            readRemaining_ = 0U;
            return 0U;
        }

        if (address != 0x68U) {
            readRemaining_ = 0U;
            return 0U;
        }

        readRemaining_ = length;
        return length;
    }

    int read() {
        if (readRemaining_ == 0U) {
            return -1;
        }

        --readRemaining_;
        return registers_[registerPointer_++];
    }

    void reset() {
        memset(registers_, 0, sizeof(registers_));
        memset(txBuffer_, 0, sizeof(txBuffer_));
        beginResult_ = true;
        began_ = false;
        failNextWrite_ = false;
        failNextRequest_ = false;
        discardWrites_ = false;
        failEndTransmissionCall_ = -1;
        endTransmissionCalls_ = 0;
        address_ = 0U;
        registerPointer_ = 0U;
        txLength_ = 0U;
        readRemaining_ = 0U;
    }

    void setBeginResult(bool result) {
        beginResult_ = result;
    }

    void failNextWrite() {
        failNextWrite_ = true;
    }

    void failNextRequest() {
        failNextRequest_ = true;
    }

    void failEndTransmissionOnCall(int call) {
        failEndTransmissionCall_ = call;
    }

    void setDiscardWrites(bool discard) {
        discardWrites_ = discard;
    }

    int endTransmissionCalls() const {
        return endTransmissionCalls_;
    }

    uint8_t& reg(uint8_t address) {
        return registers_[address];
    }

    const uint8_t& reg(uint8_t address) const {
        return registers_[address];
    }

private:
    uint8_t registers_[256] {};
    uint8_t txBuffer_[32] {};

    bool beginResult_ = true;
    bool began_ = false;
    bool failNextWrite_ = false;
    bool failNextRequest_ = false;
    bool discardWrites_ = false;

    int failEndTransmissionCall_ = -1;
    int endTransmissionCalls_ = 0;

    uint8_t address_ = 0U;
    uint8_t registerPointer_ = 0U;
    size_t txLength_ = 0U;
    size_t readRemaining_ = 0U;
};

extern TwoWire Wire;