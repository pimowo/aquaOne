#include "DoserOtaApplication.h"

#include <limits.h>

bool DoserOtaApplication::publishCapacity() {
    const size_t available = authority_.availableFirmwareSpace();
    DoserOtaCapacity value {};
    value.availableBytes = available > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(available);
    value.available = available != 0U;
    return capacity_.publish(value);
}

void DoserOtaApplication::copyDiagnostic(char out[97], const char* text) {
    size_t i = 0U;
    if (text != nullptr) for (; i < 96U && text[i] != 0; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        out[i] = c >= 32U && c < 127U ? static_cast<char>(c) : '?';
    }
    out[i] = 0;
}

void DoserOtaApplication::cleanup() {
    state_ = State::Aborting;
    if (updateBegun_) authority_.abortFirmwareUpdate();
    updateBegun_ = false;
    if (otaGuard_) authority_.setOtaInProgress(false);
    otaGuard_ = false;
    expected_ = received_ = 0U;
    restartArmed_ = false;
    state_ = State::Failed;
    state_ = State::Idle;
    generation_ = 0U;
}

DoserOtaResult DoserOtaApplication::fail(DoserOtaError error, const char* diagnostic) {
    DoserOtaResult result {};
    result.error = error;
    copyDiagnostic(result.diagnostic, diagnostic);
    cleanup();
    return result;
}

DoserOtaResult DoserOtaApplication::execute(const UploadCommand& command,
                                            const uint8_t* bytes) {
    if (command.kind == UploadOperation::Start) {
        if (state_ != State::Idle || command.generation == 0U || command.expectedSize == 0U)
            return DoserOtaResult(DoserOtaError::Invalid);
        if (restart_.isRestartPending())
            return DoserOtaResult(DoserOtaError::Busy);
        if (command.expectedSize > authority_.availableFirmwareSpace())
            return DoserOtaResult(DoserOtaError::TooLarge);
        state_ = State::Starting;
        generation_ = command.generation;
        expected_ = command.expectedSize;
        received_ = 0U;
        authority_.stopPumps();
        authority_.setOtaInProgress(true);
        otaGuard_ = true;
        if (!authority_.beginFirmwareUpdate(expected_)) {
            DoserOtaResult result {};
            result.error = DoserOtaError::UpdateFailure;
            copyDiagnostic(result.diagnostic, authority_.firmwareError());
            cleanup();
            return result;
        }
        updateBegun_ = true;
        if (bridge_.cancellationRequested(command.generation))
            return fail(DoserOtaError::Unavailable);
        state_ = State::Receiving;
        return DoserOtaResult();
    }
    if (command.generation == 0U || command.generation != generation_)
        return DoserOtaResult(DoserOtaError::Invalid);
    if (command.kind == UploadOperation::ArmRestart) {
        if (state_ != State::SucceededAwaitingRestart)
            return DoserOtaResult(DoserOtaError::Invalid);
        if (!restartArmed_) restartArmed_ = restart_.scheduleOtaRestart();
        return restartArmed_ ? DoserOtaResult() : DoserOtaResult(DoserOtaError::Unavailable);
    }
    if (state_ != State::Receiving) return DoserOtaResult(DoserOtaError::Invalid);
    if (command.kind == UploadOperation::Chunk) {
        if (bytes == nullptr || command.length == 0U || command.length > 1024U)
            return DoserOtaResult(DoserOtaError::Invalid);
        if (command.length > expected_ - received_)
            return fail(DoserOtaError::Invalid, "Rozmiar pliku jest niezgodny");
        if (authority_.writeFirmware(bytes, command.length) != command.length) {
            DoserOtaResult result {};
            result.error = DoserOtaError::UpdateFailure;
            copyDiagnostic(result.diagnostic, authority_.firmwareError());
            cleanup();
            return result;
        }
        received_ += command.length;
        if (bridge_.cancellationRequested(command.generation))
            return fail(DoserOtaError::Unavailable);
        return DoserOtaResult();
    }
    if (command.kind == UploadOperation::End) {
        if (received_ != expected_)
            return fail(DoserOtaError::Invalid, "Rozmiar pliku jest niezgodny");
        state_ = State::Finalizing;
        if (!authority_.endFirmwareUpdate()) {
            DoserOtaResult result {};
            result.error = DoserOtaError::UpdateFailure;
            copyDiagnostic(result.diagnostic, authority_.firmwareError());
            cleanup();
            return result;
        }
        updateBegun_ = false;
        finalizedAt_ = authority_.nowMs();
        state_ = State::SucceededAwaitingRestart;
        return DoserOtaResult();
    }
    return DoserOtaResult(DoserOtaError::Invalid);
}

bool DoserOtaApplication::processOne() {
    (void)publishCapacity();
    UploadCommand command {};
    const uint8_t* bytes = nullptr;
    if (bridge_.take(command, bytes)) {
        const DoserOtaResult result = bridge_.cancellationRequested(command.generation) &&
                                       command.kind != UploadOperation::ArmRestart
            ? fail(DoserOtaError::Unavailable) : execute(command, bytes);
        (void)bridge_.complete(command, result);
        if (state_ == State::Idle) (void)bridge_.releaseSession(command.generation);
        return true;
    }
    if (generation_ != 0U && state_ != State::SucceededAwaitingRestart &&
        bridge_.cancellationRequested(generation_)) {
        const uint64_t old = generation_;
        cleanup();
        (void)bridge_.releaseSession(old);
        return true;
    }
    const uint64_t active = bridge_.activeGeneration();
    if (active != 0U && state_ == State::Idle &&
        bridge_.cancellationRequested(active)) {
        (void)bridge_.releaseSession(active);
        return true;
    }
    return false;
}

void DoserOtaApplication::serviceRestartWatchdog() {
    if (state_ == State::SucceededAwaitingRestart && !restartArmed_ &&
        static_cast<uint32_t>(authority_.nowMs() - finalizedAt_) >= 30000U)
        restartArmed_ = restart_.scheduleOtaRestart();
}

bool DoserOtaApplication::blocksMqttService() const {
    return bridge_.uploadActive() || state_ == State::SucceededAwaitingRestart;
}
