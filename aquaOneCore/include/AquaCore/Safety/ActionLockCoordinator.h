#pragma once

#include <stddef.h>

namespace AquaCore {
namespace Safety {

struct ActionLockContribution {
    ActionLockContribution(bool isLocked = false, bool isSafetyCritical = false)
        : locked(isLocked), safetyCritical(isSafetyCritical) {
    }

    bool locked;
    // Meaningful only when locked is true.
    bool safetyCritical;
};

struct ActionLockState {
    ActionLockState(
        bool isLocked = false,
        bool hasSafetyCritical = false,
        size_t activeProviders = 0U,
        bool isCompositionValid = true
    ) : locked(isLocked),
        hasSafetyCriticalLock(hasSafetyCritical),
        activeProviderCount(activeProviders),
        compositionValid(isCompositionValid) {
    }

    bool locked;
    bool hasSafetyCriticalLock;
    // Counts providers with an active contribution, not underlying reasons.
    size_t activeProviderCount;
    // False means fail-closed because composition/contribution was invalid.
    bool compositionValid;
};

template <typename Action>
class ActionLockProvider {
public:
    virtual ActionLockContribution queryActionLock(const Action& action) const = 0;

protected:
    // Providers are borrowed and never destroyed through this interface.
    ~ActionLockProvider() = default;
};

template <typename Action>
class ActionLockCoordinator {
public:
    // Providers and pointer array are borrowed immutable composition and must
    // outlive this coordinator. No registration or cached lock state.
    ActionLockCoordinator(
        const ActionLockProvider<Action>* const* providers,
        size_t providerCount
    ) : providers_(providers), providerCount_(providerCount),
        compositionValid_(true) {
        if ((providerCount_ == 0U) != (providers_ == nullptr)) {
            compositionValid_ = false;
        }
        if (compositionValid_) {
            for (size_t index = 0U; index < providerCount_; ++index) {
                if (providers_[index] == nullptr) {
                    compositionValid_ = false;
                    break;
                }
            }
        }
    }

    ActionLockCoordinator(const ActionLockCoordinator&) = delete;
    ActionLockCoordinator& operator=(const ActionLockCoordinator&) = delete;
    ActionLockCoordinator(ActionLockCoordinator&&) = delete;
    ActionLockCoordinator& operator=(ActionLockCoordinator&&) = delete;

    bool isCompositionValid() const {
        return compositionValid_;
    }

    ActionLockState query(const Action& action) const {
        if (!compositionValid_) {
            return failClosed();
        }

        ActionLockState result;
        for (size_t index = 0U; index < providerCount_; ++index) {
            const ActionLockContribution contribution =
                providers_[index]->queryActionLock(action);
            if (contribution.safetyCritical && !contribution.locked) {
                return failClosed();
            }
            if (contribution.locked) {
                result.locked = true;
                ++result.activeProviderCount;
                if (contribution.safetyCritical) {
                    result.hasSafetyCriticalLock = true;
                }
            }
        }
        return result;
    }

private:
    static ActionLockState failClosed() {
        return ActionLockState(true, true, 0U, false);
    }

    const ActionLockProvider<Action>* const* providers_;
    size_t providerCount_;
    bool compositionValid_;
};

} // namespace Safety
} // namespace AquaCore
