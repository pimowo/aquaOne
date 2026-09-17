#include "AquaCore/System/ApplicationRuntime.h"

#include <string.h>

namespace AquaCore {
namespace System {
namespace {

RuntimeStatus preStartStatus() {
    RuntimeStatus value {};
    value.operational = OperationalState::BOOTING;
    value.health = HealthState::OK;
    value.safety = SafetyState::LOCKED;
    value.startupPhase = StartupPhase::BOOT;
    return value;
}

bool hasParticipantId(const char* participantId) {
    return participantId != nullptr && participantId[0] != '\0';
}

const char* validParticipantIdOrNull(const char* participantId) {
    return hasParticipantId(participantId) ? participantId : nullptr;
}

bool sameParticipantId(const char* first, const char* second) {
    return strcmp(first, second) == 0;
}

bool isValidPhase(StartupPhase phase) {
    switch (phase) {
        case StartupPhase::BOOT:
        case StartupPhase::CORE_INIT:
        case StartupPhase::LOAD_VALIDATE_CONFIG:
        case StartupPhase::HARDWARE_INIT:
        case StartupPhase::DOMAIN_INIT:
        case StartupPhase::SAFETY_VALIDATION:
        case StartupPhase::NETWORK_INIT:
        case StartupPhase::INTERFACES_INIT:
        case StartupPhase::RUNNING:
            return true;
        default:
            return false;
    }
}

bool isOrdinaryPhase(StartupPhase phase) {
    switch (phase) {
        case StartupPhase::BOOT:
        case StartupPhase::CORE_INIT:
        case StartupPhase::LOAD_VALIDATE_CONFIG:
        case StartupPhase::HARDWARE_INIT:
        case StartupPhase::DOMAIN_INIT:
        case StartupPhase::NETWORK_INIT:
        case StartupPhase::INTERFACES_INIT:
            return true;
        default:
            return false;
    }
}

uint8_t phaseOrder(StartupPhase phase) {
    return static_cast<uint8_t>(phase);
}

bool isValidRequirement(StartupRequirement requirement) {
    switch (requirement) {
        case StartupRequirement::REQUIRED:
        case StartupRequirement::OPTIONAL:
            return true;
        default:
            return false;
    }
}

StartupErrorCode invalidResultError(const StartupStepResult& result) {
    switch (result.outcome()) {
        case StartupOutcome::FAILED:
            return result.hasErrorCode()
                ? CoreStartupError::errorCodeInvalid()
                : CoreStartupError::errorCodeMissing();
        case StartupOutcome::SUCCEEDED:
        case StartupOutcome::DISABLED:
            return CoreStartupError::errorCodeUnexpected();
        default:
            return CoreStartupError::outcomeInvalid();
    }
}

} // namespace

ApplicationRuntime::ApplicationRuntime(
    ApplicationPlan plan,
    StartupFailureStorage optionalFailureStorage
) : plan_(plan),
    optionalFailureStorage_(optionalFailureStorage),
    status_(preStartStatus()),
    report_(
        optionalFailureStorage.records,
        optionalFailureStorage.capacity,
        status_
    ),
    startInProgress_(false) {
}

const StartupReport& ApplicationRuntime::start() {
    if (report_.isComplete() || startInProgress_) {
        return report_;
    }

    startInProgress_ = true;
    status_ = preStartStatus();

    if (!validateEarlySafeOutputs() || !executeEarlySafeOutputs()) {
        return report_;
    }
    if (!validateFullPlan()) {
        return report_;
    }

    if (!executePhase(StartupPhase::BOOT) ||
        !executePhase(StartupPhase::CORE_INIT) ||
        !executePhase(StartupPhase::LOAD_VALIDATE_CONFIG) ||
        !executePhase(StartupPhase::HARDWARE_INIT) ||
        !executePhase(StartupPhase::DOMAIN_INIT) ||
        !executeSafetyGate() ||
        !executePhase(StartupPhase::NETWORK_INIT) ||
        !executePhase(StartupPhase::INTERFACES_INIT)) {
        return report_;
    }

    finishSuccess();
    return report_;
}

RuntimeStatus ApplicationRuntime::status() const {
    return status_;
}

const StartupReport& ApplicationRuntime::startupReport() const {
    return report_;
}

bool ApplicationRuntime::validateEarlySafeOutputs() {
    if (plan_.earlySafeOutputs.callback == nullptr) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::EARLY_SAFE_OUTPUTS,
            validParticipantIdOrNull(plan_.earlySafeOutputs.participantId),
            false,
            0U,
            CoreStartupError::callbackMissing()
        );
        return false;
    }
    if (!hasParticipantId(plan_.earlySafeOutputs.participantId)) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::EARLY_SAFE_OUTPUTS,
            nullptr,
            false,
            0U,
            CoreStartupError::participantIdMissing()
        );
        return false;
    }
    return true;
}

bool ApplicationRuntime::executeEarlySafeOutputs() {
    const StartupStepResult result = plan_.earlySafeOutputs.callback(
        plan_.earlySafeOutputs.context
    );
    return handleSpecialResult(
        plan_.earlySafeOutputs,
        StartupFailureSource::EARLY_SAFE_OUTPUTS,
        result
    );
}

bool ApplicationRuntime::validateFullPlan() {
    if ((optionalFailureStorage_.capacity == 0U &&
         optionalFailureStorage_.records != nullptr) ||
        (optionalFailureStorage_.capacity > 0U &&
         optionalFailureStorage_.records == nullptr)) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::PLAN,
            nullptr,
            false,
            0U,
            CoreStartupError::reportStorageInvalid()
        );
        return false;
    }

    if (plan_.safetyGate.callback == nullptr) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::SAFETY_GATE,
            validParticipantIdOrNull(plan_.safetyGate.participantId),
            false,
            0U,
            CoreStartupError::callbackMissing()
        );
        return false;
    }
    if (!hasParticipantId(plan_.safetyGate.participantId)) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::SAFETY_GATE,
            nullptr,
            false,
            0U,
            CoreStartupError::participantIdMissing()
        );
        return false;
    }

    if ((plan_.participantCount == 0U && plan_.participants != nullptr) ||
        (plan_.participantCount > 0U && plan_.participants == nullptr)) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::PLAN,
            nullptr,
            false,
            0U,
            CoreStartupError::participantStorageInvalid()
        );
        return false;
    }

    if (sameParticipantId(
            plan_.earlySafeOutputs.participantId,
            plan_.safetyGate.participantId
        )) {
        fail(
            StartupFailureKind::PLAN_CONTRACT_FAILURE,
            StartupFailureSource::SAFETY_GATE,
            plan_.safetyGate.participantId,
            false,
            0U,
            CoreStartupError::participantIdDuplicate()
        );
        return false;
    }

    for (size_t index = 0U; index < plan_.participantCount; ++index) {
        const StartupParticipant& participant = plan_.participants[index];
        if (participant.action.callback == nullptr) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                validParticipantIdOrNull(participant.action.participantId),
                true,
                index,
                CoreStartupError::callbackMissing()
            );
            return false;
        }
        if (!hasParticipantId(participant.action.participantId)) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                nullptr,
                true,
                index,
                CoreStartupError::participantIdMissing()
            );
            return false;
        }
        if (!isValidPhase(participant.phase)) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::phaseInvalid()
            );
            return false;
        }
        if (!isOrdinaryPhase(participant.phase)) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::phaseNotAllowed()
            );
            return false;
        }
        if (!isValidRequirement(participant.requirement)) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::requirementInvalid()
            );
            return false;
        }
        if ((participant.phase == StartupPhase::NETWORK_INIT ||
             participant.phase == StartupPhase::INTERFACES_INIT) &&
            participant.requirement != StartupRequirement::OPTIONAL) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::phaseNotAllowed()
            );
            return false;
        }
        if (index > 0U &&
            phaseOrder(participant.phase) < phaseOrder(plan_.participants[index - 1U].phase)) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::phaseOrderInvalid()
            );
            return false;
        }

        if (sameParticipantId(
                participant.action.participantId,
                plan_.earlySafeOutputs.participantId
            ) ||
            sameParticipantId(
                participant.action.participantId,
                plan_.safetyGate.participantId
            )) {
            fail(
                StartupFailureKind::PLAN_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                index,
                CoreStartupError::participantIdDuplicate()
            );
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (sameParticipantId(
                    participant.action.participantId,
                    plan_.participants[previous].action.participantId
                )) {
                fail(
                    StartupFailureKind::PLAN_CONTRACT_FAILURE,
                    StartupFailureSource::PARTICIPANT,
                    participant.action.participantId,
                    true,
                    index,
                    CoreStartupError::participantIdDuplicate()
                );
                return false;
            }
        }
    }
    return true;
}

bool ApplicationRuntime::executePhase(StartupPhase phase) {
    status_.startupPhase = phase;
    for (size_t index = 0U; index < plan_.participantCount; ++index) {
        const StartupParticipant& participant = plan_.participants[index];
        if (participant.phase != phase) {
            continue;
        }

        const StartupStepResult result = participant.action.callback(
            participant.action.context
        );
        if (!handleParticipantResult(participant, index, result)) {
            return false;
        }
    }
    return true;
}

bool ApplicationRuntime::executeSafetyGate() {
    status_.startupPhase = StartupPhase::SAFETY_VALIDATION;
    const StartupStepResult result = plan_.safetyGate.callback(
        plan_.safetyGate.context
    );
    if (!handleSpecialResult(
            plan_.safetyGate,
            StartupFailureSource::SAFETY_GATE,
            result
        )) {
        return false;
    }
    status_.safety = SafetyState::CLEAR;
    return true;
}

bool ApplicationRuntime::handleParticipantResult(
    const StartupParticipant& participant,
    size_t participantIndex,
    const StartupStepResult& result
) {
    if (!result.isValid()) {
        fail(
            StartupFailureKind::RESULT_CONTRACT_FAILURE,
            StartupFailureSource::PARTICIPANT,
            participant.action.participantId,
            true,
            participantIndex,
            invalidResultError(result)
        );
        return false;
    }

    switch (result.outcome()) {
        case StartupOutcome::SUCCEEDED:
            return true;
        case StartupOutcome::DISABLED:
            if (participant.requirement == StartupRequirement::OPTIONAL) {
                return true;
            }
            fail(
                StartupFailureKind::RESULT_CONTRACT_FAILURE,
                StartupFailureSource::PARTICIPANT,
                participant.action.participantId,
                true,
                participantIndex,
                CoreStartupError::requiredDisabled()
            );
            return false;
        case StartupOutcome::FAILED:
            if (participant.requirement == StartupRequirement::REQUIRED) {
                fail(
                    StartupFailureKind::PARTICIPANT_FAILURE,
                    StartupFailureSource::PARTICIPANT,
                    participant.action.participantId,
                    true,
                    participantIndex,
                    *result.errorCode()
                );
                return false;
            }
            status_.health = HealthState::DEGRADED;
            report_.addOptionalFailure(
                StartupFailureSource::PARTICIPANT,
                status_.startupPhase,
                participant.action.participantId,
                participantIndex,
                *result.errorCode()
            );
            return true;
        default:
            return false;
    }
}

bool ApplicationRuntime::handleSpecialResult(
    const StartupAction& action,
    StartupFailureSource source,
    const StartupStepResult& result
) {
    if (!result.isValid()) {
        fail(
            StartupFailureKind::RESULT_CONTRACT_FAILURE,
            source,
            action.participantId,
            false,
            0U,
            invalidResultError(result)
        );
        return false;
    }

    switch (result.outcome()) {
        case StartupOutcome::SUCCEEDED:
            return true;
        case StartupOutcome::FAILED:
            fail(
                StartupFailureKind::PARTICIPANT_FAILURE,
                source,
                action.participantId,
                false,
                0U,
                *result.errorCode()
            );
            return false;
        case StartupOutcome::DISABLED:
            fail(
                StartupFailureKind::RESULT_CONTRACT_FAILURE,
                source,
                action.participantId,
                false,
                0U,
                CoreStartupError::specialDisabled()
            );
            return false;
        default:
            return false;
    }
}

void ApplicationRuntime::fail(
    StartupFailureKind kind,
    StartupFailureSource source,
    const char* participantId,
    bool hasParticipantIndex,
    size_t participantIndex,
    StartupErrorCode errorCode
) {
    status_.operational = OperationalState::ERROR;
    status_.health = HealthState::FAULT;
    status_.safety = SafetyState::LOCKED;
    report_.setFatalFailure(
        kind,
        source,
        status_.startupPhase,
        participantId,
        hasParticipantIndex,
        participantIndex,
        errorCode
    );
    report_.complete(status_);
    startInProgress_ = false;
}

void ApplicationRuntime::finishSuccess() {
    status_.startupPhase = StartupPhase::RUNNING;
    status_.operational = OperationalState::RUNNING;
    status_.safety = SafetyState::CLEAR;
    report_.complete(status_);
    startInProgress_ = false;
}

} // namespace System
} // namespace AquaCore
