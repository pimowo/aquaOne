#pragma once

#include "AquaCore/Commands/CommandExecutionResult.h"

namespace AquaCore {
namespace Commands {

// All callbacks are required. Contexts are borrowed and may be nullptr for
// stateless callbacks; a used context must outlive the pipeline and calls.
template <typename Command>
struct CommandPipelineConfig {
    using Validator = bool (*)(const Command&, void*);
    using Gate = bool (*)(const Command&, void*);
    using Handler = DomainCommandResult (*)(const Command&, void*);

    Validator validator = nullptr;
    void* validatorContext = nullptr;
    Gate policy = nullptr;
    void* policyContext = nullptr;
    Gate safety = nullptr;
    void* safetyContext = nullptr;
    Handler handler = nullptr;
    void* handlerContext = nullptr;
};

// The command is borrowed only for execute(); the pipeline never stores it.
template <typename Command>
class CommandPipeline {
public:
    explicit CommandPipeline(
        const CommandPipelineConfig<Command>& config
    ) : config_(config) {
    }

    bool isValid() const {
        return config_.validator != nullptr &&
               config_.policy != nullptr &&
               config_.safety != nullptr &&
               config_.handler != nullptr;
    }

    CommandExecutionResult execute(const Command& command) const {
        if (!isValid()) {
            return CommandExecutionResult::invalidPipeline();
        }
        if (!config_.validator(command, config_.validatorContext)) {
            return CommandExecutionResult::invalidCommand();
        }
        if (!config_.policy(command, config_.policyContext)) {
            return CommandExecutionResult::blockedByPolicy();
        }
        if (!config_.safety(command, config_.safetyContext)) {
            return CommandExecutionResult::blockedBySafety();
        }
        return CommandExecutionResult::handled(
            config_.handler(command, config_.handlerContext)
        );
    }

private:
    CommandPipelineConfig<Command> config_;
};

} // namespace Commands
} // namespace AquaCore
