#include "StreamingUploadBridge.h"

#include <limits.h>
#include <string.h>

UploadAdmission StreamingUploadBridge::admitSession(uint64_t& generation) {
    if (!sync_.lock()) return UploadAdmission::Unavailable;
    UploadAdmission result = UploadAdmission::Busy;
    if (activeGeneration_ == 0U && state_ == UploadSlotState::Free) {
        result = lastGeneration_ == UINT64_MAX ? UploadAdmission::Exhausted
                                                : UploadAdmission::Accepted;
    }
    if (result == UploadAdmission::Accepted) {
        generation = ++lastGeneration_;
        activeGeneration_ = generation;
        cancelRequested_ = false;
    }
    sync_.unlock();
    return result;
}

bool StreamingUploadBridge::beginSession(uint64_t& generation) {
    return admitSession(generation) == UploadAdmission::Accepted;
}

UploadSessionView StreamingUploadBridge::sessionView() const {
    if (!sync_.lock()) return UploadSessionView::Unavailable;
    const UploadSessionView view = activeGeneration_ == 0U
        ? UploadSessionView::Idle : UploadSessionView::Busy;
    sync_.unlock();
    return view;
}

bool StreamingUploadBridge::uploadActive() const {
    return sessionView() != UploadSessionView::Idle;
}

uint64_t StreamingUploadBridge::activeGeneration() const {
    if (!sync_.lock()) return 0U;
    const uint64_t result = activeGeneration_;
    sync_.unlock();
    return result;
}

bool StreamingUploadBridge::requestCancel(uint64_t generation) {
    if (generation == 0U || !sync_.lock()) return false;
    const bool matches = activeGeneration_ == generation;
    if (matches) cancelRequested_ = true;
    sync_.unlock();
    return matches;
}

bool StreamingUploadBridge::cancellationRequested(uint64_t generation) const {
    if (!sync_.lock()) return true;
    const bool canceled = activeGeneration_ == generation && cancelRequested_;
    sync_.unlock();
    return canceled;
}

bool StreamingUploadBridge::releaseSession(uint64_t generation) {
    if (!sync_.lock()) return false;
    const bool released = generation != 0U && activeGeneration_ == generation &&
                          state_ != UploadSlotState::Processing &&
                          state_ != UploadSlotState::Queued;
    if (released) {
        activeGeneration_ = 0U;
        cancelRequested_ = false;
    }
    sync_.unlock();
    return released;
}

bool StreamingUploadBridge::submit(const UploadCommand& command, const uint8_t* bytes) {
    if (command.generation == 0U || command.length > sizeof(data_) ||
        (command.kind == UploadOperation::Chunk && (command.length == 0U || bytes == nullptr)) ||
        (command.kind != UploadOperation::Chunk && command.length != 0U) || !sync_.lock()) return false;
    const bool accepted = sync_.completionSlotCapacity() >= 1U &&
                          command.generation == activeGeneration_ && !cancelRequested_ &&
                          state_ == UploadSlotState::Free && sync_.prepareCompletion(0U);
    if (accepted) {
        command_ = command;
        if (command.kind == UploadOperation::Chunk) memcpy(data_, bytes, command.length);
        state_ = UploadSlotState::Queued;
        waiterAttached_ = true;
    }
    sync_.unlock();
    return accepted;
}

UploadWaitResult StreamingUploadBridge::wait(uint64_t generation, uint32_t timeoutMs,
                                              DoserOtaResult& result) {
    if (!sync_.lock()) return UploadWaitResult::Unavailable;
    if (generation == 0U || command_.generation != generation || !waiterAttached_) {
        sync_.unlock(); return UploadWaitResult::Unavailable;
    }
    if (state_ == UploadSlotState::Completed) {
        result = result_; state_ = UploadSlotState::Free; waiterAttached_ = false;
        sync_.unlock(); return UploadWaitResult::Completed;
    }
    sync_.unlock();
    const AquaCore::Web::ActionBridgeWaitStatus status = sync_.waitForCompletion(0U, timeoutMs);
    if (!sync_.lock()) return UploadWaitResult::Unavailable;
    if (command_.generation != generation || !waiterAttached_) {
        sync_.unlock(); return UploadWaitResult::Unavailable;
    }
    if (state_ == UploadSlotState::Completed) {
        result = result_; state_ = UploadSlotState::Free; waiterAttached_ = false;
        sync_.unlock(); return UploadWaitResult::Completed;
    }
    const bool arm = command_.kind == UploadOperation::ArmRestart;
    waiterAttached_ = false;
    if (!arm) cancelRequested_ = true;
    sync_.unlock();
    return status == AquaCore::Web::ActionBridgeWaitStatus::TimedOut
        ? UploadWaitResult::TimedOut : UploadWaitResult::Unavailable;
}

bool StreamingUploadBridge::take(UploadCommand& command, const uint8_t*& bytes) {
    if (!sync_.lock()) return false;
    const bool ready = state_ == UploadSlotState::Queued;
    if (ready) {
        command = command_;
        bytes = command.kind == UploadOperation::Chunk ? data_ : nullptr;
        state_ = UploadSlotState::Processing;
    }
    sync_.unlock();
    return ready;
}

bool StreamingUploadBridge::complete(const UploadCommand& command,
                                      const DoserOtaResult& result) {
    if (!sync_.lock()) return false;
    const bool valid = state_ == UploadSlotState::Processing &&
                       command_.generation == command.generation &&
                       command_.kind == command.kind;
    if (valid) {
        if (waiterAttached_) {
            result_ = result;
            state_ = UploadSlotState::Completed;
            (void)sync_.signalCompletion(0U);
        } else state_ = UploadSlotState::Free;
    }
    sync_.unlock();
    return valid;
}

UploadSlotState StreamingUploadBridge::slotState() const {
    if (!sync_.lock()) return UploadSlotState::Processing;
    const UploadSlotState state = state_;
    sync_.unlock();
    return state;
}
