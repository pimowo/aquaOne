#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/Web/WebActionBridge.h"

namespace AquaCore {
namespace Web {

enum class NativeActionState : uint8_t {
    NotAccepted,
    Completed,
    AcceptedOutcomeUnknown,
    BridgeFailure
};

// Transport-neutral semantic result. candidateHttpStatus == 0 means that the
// route-specific mapper must choose a status. Other values are provisional
// hints only; F9.7C does not define a global response envelope.
struct NativeActionResult {
    NativeActionState state = NativeActionState::BridgeFailure;
    bool accepted = false;
    bool outcomeKnown = false;
    bool tokenProtocolClosed = true;
    uint16_t candidateHttpStatus = 0U;
    Commands::CommandExecutionResult executionResult =
        Commands::CommandExecutionResult::invalidPipeline();
};

// HTTP-side adapter only: copies a typed command into the existing bridge,
// then performs a bounded wait. Application code remains responsible for
// calling bridge.processOne() from its serialized tick/context.
template <typename Command, size_t Capacity>
class NativeActionBoundary {
public:
    explicit NativeActionBoundary(WebActionBridge<Command, Capacity>& bridge)
        : bridge_(bridge) {}

    NativeActionResult submitAndWait(const Command& command,
                                     uint32_t timeoutMs) {
        ActionBridgeToken token;
        const ActionBridgeSubmitResult submitted = bridge_.submit(command, token);
        if (submitted == ActionBridgeSubmitResult::QueueFull) {
            return notAccepted(NativeActionState::NotAccepted);
        }
        if (submitted == ActionBridgeSubmitResult::SynchronizationFailure) {
            return notAccepted(NativeActionState::BridgeFailure);
        }

        Commands::CommandExecutionResult execution =
            Commands::CommandExecutionResult::invalidPipeline();
        const ActionBridgeWaitResult waited =
            bridge_.wait(token, timeoutMs, execution);
        if (waited == ActionBridgeWaitResult::Completed) {
            NativeActionResult result;
            result.state = NativeActionState::Completed;
            result.accepted = true;
            result.outcomeKnown = true;
            result.executionResult = execution;
            const Commands::DomainCommandResult* domain =
                execution.domainResult();
            result.candidateHttpStatus =
                domain != nullptr &&
                *domain == Commands::DomainCommandResult::OperationStarted
                    ? 202U : 0U;
            return result;
        }
        if (waited == ActionBridgeWaitResult::TimedOutAccepted) {
            NativeActionResult result;
            result.state = NativeActionState::AcceptedOutcomeUnknown;
            result.accepted = true;
            result.outcomeKnown = false;
            result.candidateHttpStatus = 202U;
            // wait() detached this producer. A later processOne() releases the
            // slot after executing the already accepted command.
            return result;
        }

        // wait() deliberately leaves an anomalous accepted token attached.
        // Explicit abandon closes that protocol; it never cancels execution.
        ActionBridgeAbandonResult abandoned = bridge_.abandon(token);
        // One bounded retry covers a transient synchronizer failure without
        // turning cleanup into an unbounded HTTPD wait.
        if (abandoned == ActionBridgeAbandonResult::SynchronizationFailure) {
            abandoned = bridge_.abandon(token);
        }
        NativeActionResult result;
        result.state = NativeActionState::BridgeFailure;
        result.accepted = true;
        result.outcomeKnown = false;
        // InvalidToken is an invariant failure, but it also proves this token
        // no longer owns an attached waiter. Preserve acceptance while marking
        // the producer side of the protocol closed.
        result.tokenProtocolClosed =
            abandoned != ActionBridgeAbandonResult::SynchronizationFailure;
        result.candidateHttpStatus = 202U;
        return result;
    }

private:
    static NativeActionResult notAccepted(NativeActionState state) {
        NativeActionResult result;
        result.state = state;
        result.accepted = false;
        result.outcomeKnown = false;
        result.candidateHttpStatus = 503U;
        return result;
    }

    WebActionBridge<Command, Capacity>& bridge_;
};

} // namespace Web
} // namespace AquaCore
