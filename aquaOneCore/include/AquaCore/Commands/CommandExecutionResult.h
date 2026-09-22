#pragma once

#include <stdint.h>

#include "AquaCore/Commands/DomainCommandResult.h"

namespace AquaCore {
namespace Commands {

enum class CommandExecutionOutcome : uint8_t {
    Handled,
    InvalidCommand,
    BlockedByPolicy,
    BlockedBySafety,
    InvalidPipeline
};

class CommandExecutionResult {
public:
    static CommandExecutionResult handled(
        DomainCommandResult domainResult
    );
    static CommandExecutionResult invalidCommand();
    static CommandExecutionResult blockedByPolicy();
    static CommandExecutionResult blockedBySafety();
    static CommandExecutionResult invalidPipeline();

    CommandExecutionOutcome outcome() const;
    bool hasDomainResult() const;
    const DomainCommandResult* domainResult() const;

private:
    CommandExecutionResult(
        CommandExecutionOutcome outcome,
        bool hasDomainResult,
        DomainCommandResult domainResult
    );

    CommandExecutionOutcome outcome_;
    bool hasDomainResult_;
    DomainCommandResult domainResult_;
};

} // namespace Commands
} // namespace AquaCore
