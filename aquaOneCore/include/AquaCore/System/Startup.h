#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/System/SystemState.h"

namespace AquaCore {
namespace System {

enum class StartupRequirement : uint8_t {
    REQUIRED,
    OPTIONAL
};

enum class StartupOutcome : uint8_t {
    SUCCEEDED,
    DISABLED,
    FAILED
};

class StartupErrorCode {
public:
    StartupErrorCode() = delete;

    static StartupErrorCode fromStatic(
        const char* ownerNamespace,
        const char* localCode
    );

    bool isValid() const;
    const char* ownerNamespace() const;
    const char* localCode() const;
    bool equals(const StartupErrorCode& other) const;

private:
    StartupErrorCode(const char* ownerNamespace, const char* localCode);

    const char* ownerNamespace_;
    const char* localCode_;

    friend class StartupStepResult;
};

namespace CoreStartupError {

StartupErrorCode participantStorageInvalid();
StartupErrorCode reportStorageInvalid();
StartupErrorCode callbackMissing();
StartupErrorCode participantIdMissing();
StartupErrorCode participantIdDuplicate();
StartupErrorCode phaseInvalid();
StartupErrorCode phaseNotAllowed();
StartupErrorCode phaseOrderInvalid();
StartupErrorCode requirementInvalid();
StartupErrorCode outcomeInvalid();
StartupErrorCode errorCodeMissing();
StartupErrorCode errorCodeInvalid();
StartupErrorCode errorCodeUnexpected();
StartupErrorCode requiredDisabled();
StartupErrorCode specialDisabled();

} // namespace CoreStartupError

class StartupStepResult {
public:
    StartupStepResult() = delete;

    static StartupStepResult succeeded();
    static StartupStepResult disabled();
    static StartupStepResult failed(StartupErrorCode code);

    StartupOutcome outcome() const;
    bool isValid() const;
    bool hasErrorCode() const;
    const StartupErrorCode* errorCode() const;

private:
    StartupStepResult(StartupOutcome outcome, StartupErrorCode code);

    StartupOutcome outcome_;
    StartupErrorCode code_;
};

enum class StartupFailureKind : uint8_t {
    PARTICIPANT_FAILURE,
    PLAN_CONTRACT_FAILURE,
    RESULT_CONTRACT_FAILURE
};

enum class StartupFailureSource : uint8_t {
    PLAN,
    EARLY_SAFE_OUTPUTS,
    PARTICIPANT,
    SAFETY_GATE
};

class StartupFailureRecord {
public:
    StartupFailureRecord();

    bool isPresent() const;
    StartupFailureKind kind() const;
    StartupFailureSource source() const;
    StartupPhase phase() const;
    const char* participantId() const;
    bool hasParticipantIndex() const;
    // Valid only when hasParticipantIndex() is true.
    size_t participantIndex() const;
    const StartupErrorCode& errorCode() const;

private:
    void set(
        StartupFailureKind kind,
        StartupFailureSource source,
        StartupPhase phase,
        const char* participantId,
        bool hasParticipantIndex,
        size_t participantIndex,
        StartupErrorCode errorCode
    );

    bool present_;
    StartupFailureKind kind_;
    StartupFailureSource source_;
    StartupPhase phase_;
    const char* participantId_;
    bool hasParticipantIndex_;
    size_t participantIndex_;
    StartupErrorCode errorCode_;

    friend class ApplicationRuntime;
    friend class StartupReport;
};

struct RuntimeStatus {
    OperationalState operational;
    HealthState health;
    SafetyState safety;
    StartupPhase startupPhase;
};

using StartupCallback = StartupStepResult (*)(void* context);

struct StartupAction {
    const char* participantId;
    StartupCallback callback;
    void* context;
};

struct StartupParticipant {
    StartupAction action;
    StartupPhase phase;
    StartupRequirement requirement;
};

struct ApplicationPlan {
    StartupAction earlySafeOutputs;
    StartupAction safetyGate;
    const StartupParticipant* participants;
    size_t participantCount;
};

struct StartupFailureStorage {
    StartupFailureRecord* records;
    size_t capacity;
};

class StartupReport {
public:
    bool isComplete() const;
    // Precondition: isComplete().
    RuntimeStatus finalStatus() const;

    bool hasFatalFailure() const;
    const StartupFailureRecord* fatalFailure() const;

    size_t optionalFailureCount() const;
    size_t storedOptionalFailureCount() const;
    bool optionalFailuresTruncated() const;
    // Points into borrowed storage; stored records are read-only after completion.
    const StartupFailureRecord* optionalFailures() const;
    const StartupFailureRecord* optionalFailure(size_t index) const;
    size_t totalFailureCount() const;

private:
    StartupReport(
        StartupFailureRecord* optionalFailureRecords,
        size_t optionalFailureCapacity,
        RuntimeStatus initialStatus
    );

    void addOptionalFailure(
        StartupFailureSource source,
        StartupPhase phase,
        const char* participantId,
        size_t participantIndex,
        StartupErrorCode errorCode
    );
    void setFatalFailure(
        StartupFailureKind kind,
        StartupFailureSource source,
        StartupPhase phase,
        const char* participantId,
        bool hasParticipantIndex,
        size_t participantIndex,
        StartupErrorCode errorCode
    );
    void complete(RuntimeStatus finalStatus);

    bool complete_;
    RuntimeStatus finalStatus_;
    StartupFailureRecord fatalFailure_;
    StartupFailureRecord* optionalFailureRecords_;
    size_t optionalFailureCapacity_;
    size_t optionalFailureCount_;
    size_t storedOptionalFailureCount_;

    friend class ApplicationRuntime;
};

} // namespace System
} // namespace AquaCore
