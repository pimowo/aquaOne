#include "AquaCore/Commands/CommandExecutionResult.h"

namespace AquaCore {
namespace Commands {

CommandExecutionResult::CommandExecutionResult(
    CommandExecutionOutcome outcome,
    bool hasDomainResult,
    DomainCommandResult domainResult
) : outcome_(outcome),
    hasDomainResult_(hasDomainResult),
    domainResult_(domainResult) {
}

CommandExecutionResult CommandExecutionResult::handled(
    DomainCommandResult domainResult
) {
    if (!isDomainCommandResultValid(domainResult)) {
        return invalidPipeline();
    }
    return CommandExecutionResult(
        CommandExecutionOutcome::Handled,
        true,
        domainResult
    );
}

CommandExecutionResult CommandExecutionResult::invalidCommand() {
    return CommandExecutionResult(
        CommandExecutionOutcome::InvalidCommand,
        false,
        DomainCommandResult::Completed
    );
}

CommandExecutionResult CommandExecutionResult::blockedByPolicy() {
    return CommandExecutionResult(
        CommandExecutionOutcome::BlockedByPolicy,
        false,
        DomainCommandResult::Completed
    );
}

CommandExecutionResult CommandExecutionResult::blockedBySafety() {
    return CommandExecutionResult(
        CommandExecutionOutcome::BlockedBySafety,
        false,
        DomainCommandResult::Completed
    );
}

CommandExecutionResult CommandExecutionResult::invalidPipeline() {
    return CommandExecutionResult(
        CommandExecutionOutcome::InvalidPipeline,
        false,
        DomainCommandResult::Completed
    );
}

CommandExecutionOutcome CommandExecutionResult::outcome() const {
    return outcome_;
}

bool CommandExecutionResult::hasDomainResult() const {
    return hasDomainResult_;
}

const DomainCommandResult* CommandExecutionResult::domainResult() const {
    return hasDomainResult_ ? &domainResult_ : nullptr;
}

} // namespace Commands
} // namespace AquaCore
