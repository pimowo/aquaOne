#pragma once

#include <stddef.h>
#include <stdint.h>

#include <AquaCore/Web/ActionBridgeSynchronizer.h>

enum class DoserOtaError : uint8_t {
    None, Invalid, Busy, TooLarge, UpdateFailure, Unavailable
};

struct DoserOtaResult {
    explicit DoserOtaResult(DoserOtaError value = DoserOtaError::None) : error(value) {}
    DoserOtaError error;
    char diagnostic[97] {};
};

enum class UploadOperation : uint8_t { Start, Chunk, End, ArmRestart };
enum class UploadSlotState : uint8_t { Free, Queued, Processing, Completed };
enum class UploadWaitResult : uint8_t { Completed, TimedOut, Unavailable };
enum class UploadAdmission : uint8_t { Accepted, Busy, Unavailable, Exhausted };
enum class UploadSessionView : uint8_t { Idle, Busy, Unavailable };

struct UploadCommand {
    UploadOperation kind = UploadOperation::Start;
    uint64_t generation = 0U;
    uint32_t expectedSize = 0U;
    uint16_t length = 0U;
};

// One copy of firmware data, one operation, no pipeline. The synchronizer is
// borrowed and must remain alive until both task contexts have stopped.
class StreamingUploadBridge {
public:
    explicit StreamingUploadBridge(AquaCore::Web::ActionBridgeSynchronizer& sync,
                                   uint64_t lastGeneration = 0U)
        : sync_(sync), lastGeneration_(lastGeneration) {}
    UploadAdmission admitSession(uint64_t& generation);
    bool beginSession(uint64_t& generation);
    UploadSessionView sessionView() const;
    bool uploadActive() const;
    uint64_t activeGeneration() const;
    bool requestCancel(uint64_t generation);
    bool cancellationRequested(uint64_t generation) const;
    bool releaseSession(uint64_t generation);
    bool submit(const UploadCommand& command, const uint8_t* bytes = nullptr);
    UploadWaitResult wait(uint64_t generation, uint32_t timeoutMs, DoserOtaResult& result);
    bool take(UploadCommand& command, const uint8_t*& bytes);
    bool complete(const UploadCommand& command, const DoserOtaResult& result);
    UploadSlotState slotState() const;
    static constexpr size_t chunkCapacity() { return 1024U; }

private:
    AquaCore::Web::ActionBridgeSynchronizer& sync_;
    uint8_t data_[1024] {};
    UploadCommand command_ {};
    DoserOtaResult result_ {};
    uint64_t lastGeneration_ = 0U;
    uint64_t activeGeneration_ = 0U;
    UploadSlotState state_ = UploadSlotState::Free;
    bool cancelRequested_ = false;
    bool waiterAttached_ = false;
};
