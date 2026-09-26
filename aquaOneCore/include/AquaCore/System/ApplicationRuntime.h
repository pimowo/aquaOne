#pragma once

#include "AquaCore/System/Startup.h"

namespace AquaCore {
namespace System {

class RuntimeStateCoordinator;

class ApplicationRuntime {
public:
    // Non-copyable/non-movable: plan, contexts and failure storage are borrowed.
    ApplicationRuntime(
        ApplicationPlan plan,
        StartupFailureStorage optionalFailureStorage
    );

    ApplicationRuntime(const ApplicationRuntime&) = delete;
    ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;
    ApplicationRuntime(ApplicationRuntime&&) = delete;
    ApplicationRuntime& operator=(ApplicationRuntime&&) = delete;

    // One-shot and non-reentrant; borrowed inputs must outlive this runtime.
    const StartupReport& start();
    RuntimeStatus status() const;
    const StartupReport& startupReport() const;
    bool handoffRuntimeState(RuntimeStateCoordinator& coordinator);
    bool runtimeStateHandedOff() const;

private:
    bool validateEarlySafeOutputs();
    bool validateFullPlan();
    bool executeEarlySafeOutputs();
    bool executeSafetyGate();
    bool executePhase(StartupPhase phase);
    bool handleParticipantResult(
        const StartupParticipant& participant,
        size_t participantIndex,
        const StartupStepResult& result
    );
    bool handleSpecialResult(
        const StartupAction& action,
        StartupFailureSource source,
        const StartupStepResult& result
    );
    void fail(
        StartupFailureKind kind,
        StartupFailureSource source,
        const char* participantId,
        bool hasParticipantIndex,
        size_t participantIndex,
        StartupErrorCode errorCode
    );
    void finishSuccess();

    ApplicationPlan plan_;
    StartupFailureStorage optionalFailureStorage_;
    RuntimeStatus status_;
    StartupReport report_;
    bool startInProgress_;
    bool runtimeStateHandedOff_;
};

} // namespace System
} // namespace AquaCore
