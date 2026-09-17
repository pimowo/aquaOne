#include "AquaCore/System/Startup.h"

#include <string.h>

namespace AquaCore {
namespace System {
namespace {

bool isUppercaseLetter(char value) {
    return value >= 'A' && value <= 'Z';
}

bool isTokenTail(char value) {
    return isUppercaseLetter(value) ||
        (value >= '0' && value <= '9') || value == '_';
}

bool isLocalCodeValid(const char* value) {
    if (value == nullptr || !isUppercaseLetter(value[0])) {
        return false;
    }
    for (size_t index = 1U; value[index] != '\0'; ++index) {
        if (!isTokenTail(value[index])) {
            return false;
        }
    }
    return true;
}

bool isNamespaceValid(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    bool expectsSegmentStart = true;
    for (size_t index = 0U; value[index] != '\0'; ++index) {
        const char current = value[index];
        if (expectsSegmentStart) {
            if (!isUppercaseLetter(current)) {
                return false;
            }
            expectsSegmentStart = false;
        } else if (current == '.') {
            expectsSegmentStart = true;
        } else if (!isTokenTail(current)) {
            return false;
        }
    }
    return !expectsSegmentStart;
}

StartupErrorCode coreCode(const char* localCode) {
    return StartupErrorCode::fromStatic("AQUA.CORE", localCode);
}

} // namespace

StartupErrorCode::StartupErrorCode(const char* ownerNamespace,
    const char* localCode)
    : ownerNamespace_(ownerNamespace), localCode_(localCode) {
}

StartupErrorCode StartupErrorCode::fromStatic(const char* ownerNamespace,
    const char* localCode) {
    return StartupErrorCode(ownerNamespace, localCode);
}

bool StartupErrorCode::isValid() const {
    return isNamespaceValid(ownerNamespace_) && isLocalCodeValid(localCode_);
}

const char* StartupErrorCode::ownerNamespace() const {
    return ownerNamespace_;
}

const char* StartupErrorCode::localCode() const {
    return localCode_;
}

bool StartupErrorCode::equals(const StartupErrorCode& other) const {
    return isValid() && other.isValid() &&
        strcmp(ownerNamespace_, other.ownerNamespace_) == 0 &&
        strcmp(localCode_, other.localCode_) == 0;
}

namespace CoreStartupError {

StartupErrorCode participantStorageInvalid() {
    return coreCode("PARTICIPANT_STORAGE_INVALID");
}
StartupErrorCode reportStorageInvalid() {
    return coreCode("REPORT_STORAGE_INVALID");
}
StartupErrorCode callbackMissing() {
    return coreCode("CALLBACK_MISSING");
}
StartupErrorCode participantIdMissing() {
    return coreCode("PARTICIPANT_ID_MISSING");
}
StartupErrorCode participantIdDuplicate() {
    return coreCode("PARTICIPANT_ID_DUPLICATE");
}
StartupErrorCode phaseInvalid() {
    return coreCode("PHASE_INVALID");
}
StartupErrorCode phaseNotAllowed() {
    return coreCode("PHASE_NOT_ALLOWED");
}
StartupErrorCode phaseOrderInvalid() {
    return coreCode("PHASE_ORDER_INVALID");
}
StartupErrorCode requirementInvalid() {
    return coreCode("REQUIREMENT_INVALID");
}
StartupErrorCode outcomeInvalid() {
    return coreCode("OUTCOME_INVALID");
}
StartupErrorCode errorCodeMissing() {
    return coreCode("ERROR_CODE_MISSING");
}
StartupErrorCode errorCodeInvalid() {
    return coreCode("ERROR_CODE_INVALID");
}
StartupErrorCode errorCodeUnexpected() {
    return coreCode("ERROR_CODE_UNEXPECTED");
}
StartupErrorCode requiredDisabled() {
    return coreCode("REQUIRED_DISABLED");
}
StartupErrorCode specialDisabled() {
    return coreCode("SPECIAL_DISABLED");
}

} // namespace CoreStartupError

StartupStepResult::StartupStepResult(StartupOutcome outcome,
    StartupErrorCode code)
    : outcome_(outcome), code_(code) {
}

StartupStepResult StartupStepResult::succeeded() {
    return StartupStepResult(StartupOutcome::SUCCEEDED,
        StartupErrorCode(nullptr, nullptr));
}

StartupStepResult StartupStepResult::disabled() {
    return StartupStepResult(StartupOutcome::DISABLED,
        StartupErrorCode(nullptr, nullptr));
}

StartupStepResult StartupStepResult::failed(StartupErrorCode code) {
    return StartupStepResult(StartupOutcome::FAILED, code);
}

StartupOutcome StartupStepResult::outcome() const {
    return outcome_;
}

bool StartupStepResult::isValid() const {
    switch (outcome_) {
        case StartupOutcome::SUCCEEDED:
        case StartupOutcome::DISABLED:
            return !hasErrorCode();
        case StartupOutcome::FAILED:
            return hasErrorCode() && code_.isValid();
        default:
            return false;
    }
}

bool StartupStepResult::hasErrorCode() const {
    return code_.ownerNamespace() != nullptr || code_.localCode() != nullptr;
}

const StartupErrorCode* StartupStepResult::errorCode() const {
    return hasErrorCode() ? &code_ : nullptr;
}

StartupFailureRecord::StartupFailureRecord()
    : present_(false),
      kind_(StartupFailureKind::PLAN_CONTRACT_FAILURE),
      source_(StartupFailureSource::PLAN),
      phase_(StartupPhase::BOOT),
      participantId_(nullptr),
      hasParticipantIndex_(false),
      participantIndex_(0U),
      errorCode_(StartupErrorCode::fromStatic(nullptr, nullptr)) {
}

bool StartupFailureRecord::isPresent() const { return present_; }
StartupFailureKind StartupFailureRecord::kind() const { return kind_; }
StartupFailureSource StartupFailureRecord::source() const { return source_; }
StartupPhase StartupFailureRecord::phase() const { return phase_; }
const char* StartupFailureRecord::participantId() const { return participantId_; }
bool StartupFailureRecord::hasParticipantIndex() const { return hasParticipantIndex_; }
size_t StartupFailureRecord::participantIndex() const { return participantIndex_; }
const StartupErrorCode& StartupFailureRecord::errorCode() const { return errorCode_; }

void StartupFailureRecord::set(StartupFailureKind kind,
    StartupFailureSource source, StartupPhase phase,
    const char* participantId, bool hasParticipantIndex,
    size_t participantIndex, StartupErrorCode errorCode) {
    present_ = true;
    kind_ = kind;
    source_ = source;
    phase_ = phase;
    participantId_ = participantId;
    hasParticipantIndex_ = hasParticipantIndex;
    participantIndex_ = participantIndex;
    errorCode_ = errorCode;
}

StartupReport::StartupReport(StartupFailureRecord* optionalFailureRecords,
    size_t optionalFailureCapacity, RuntimeStatus initialStatus)
    : complete_(false),
      finalStatus_(initialStatus),
      fatalFailure_(),
      optionalFailureRecords_(optionalFailureRecords),
      optionalFailureCapacity_(optionalFailureCapacity),
      optionalFailureCount_(0U),
      storedOptionalFailureCount_(0U) {
}

bool StartupReport::isComplete() const { return complete_; }
RuntimeStatus StartupReport::finalStatus() const { return finalStatus_; }
bool StartupReport::hasFatalFailure() const { return fatalFailure_.isPresent(); }

const StartupFailureRecord* StartupReport::fatalFailure() const {
    return hasFatalFailure() ? &fatalFailure_ : nullptr;
}

size_t StartupReport::optionalFailureCount() const {
    return optionalFailureCount_;
}
size_t StartupReport::storedOptionalFailureCount() const {
    return storedOptionalFailureCount_;
}
bool StartupReport::optionalFailuresTruncated() const {
    return optionalFailureCount_ > storedOptionalFailureCount_;
}
const StartupFailureRecord* StartupReport::optionalFailures() const {
    return storedOptionalFailureCount_ > 0U ? optionalFailureRecords_ : nullptr;
}
const StartupFailureRecord* StartupReport::optionalFailure(size_t index) const {
    return index < storedOptionalFailureCount_
        ? &optionalFailureRecords_[index] : nullptr;
}
size_t StartupReport::totalFailureCount() const {
    return optionalFailureCount_ + (hasFatalFailure() ? 1U : 0U);
}

void StartupReport::addOptionalFailure(StartupFailureSource source,
    StartupPhase phase, const char* participantId, size_t participantIndex,
    StartupErrorCode errorCode) {
    ++optionalFailureCount_;
    if (storedOptionalFailureCount_ >= optionalFailureCapacity_) {
        return;
    }
    optionalFailureRecords_[storedOptionalFailureCount_].set(
        StartupFailureKind::PARTICIPANT_FAILURE, source, phase,
        participantId, true, participantIndex, errorCode);
    ++storedOptionalFailureCount_;
}

void StartupReport::setFatalFailure(StartupFailureKind kind,
    StartupFailureSource source, StartupPhase phase,
    const char* participantId, bool hasParticipantIndex,
    size_t participantIndex, StartupErrorCode errorCode) {
    fatalFailure_.set(kind, source, phase, participantId,
        hasParticipantIndex, participantIndex, errorCode);
}

void StartupReport::complete(RuntimeStatus finalStatus) {
    finalStatus_ = finalStatus;
    complete_ = true;
}

} // namespace System
} // namespace AquaCore
