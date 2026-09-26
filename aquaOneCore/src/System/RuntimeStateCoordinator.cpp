#include <AquaCore/System/RuntimeStateCoordinator.h>

namespace AquaCore {
namespace System {
namespace {

bool validHealth(HealthState value) {
    switch (value) {
        case HealthState::OK:
        case HealthState::DEGRADED:
        case HealthState::FAULT:
            return true;
        default:
            return false;
    }
}

bool validSafety(SafetyState value) {
    switch (value) {
        case SafetyState::CLEAR:
        case SafetyState::LOCKED:
            return true;
        default:
            return false;
    }
}

uint8_t healthSeverity(HealthState value) {
    switch (value) {
        case HealthState::FAULT:
            return 2U;
        case HealthState::DEGRADED:
            return 1U;
        case HealthState::OK:
        default:
            return 0U;
    }
}

} // namespace

RuntimeStateCoordinator::RuntimeStateCoordinator(
    const HealthProvider* const* healthProviders,
    size_t healthProviderCount,
    const SafetyProvider* const* safetyProviders,
    size_t safetyProviderCount
) : healthProviders_(healthProviders),
    healthProviderCount_(healthProviderCount),
    safetyProviders_(safetyProviders),
    safetyProviderCount_(safetyProviderCount),
    status_(nullptr),
    providerListsValid_(true),
    active_(false) {
    if ((healthProviderCount_ == 0U) != (healthProviders_ == nullptr) ||
        (safetyProviderCount_ == 0U) != (safetyProviders_ == nullptr)) {
        providerListsValid_ = false;
    }
    if (providerListsValid_) {
        for (size_t index = 0U; index < healthProviderCount_; ++index) {
            if (healthProviders_[index] == nullptr) {
                providerListsValid_ = false;
                break;
            }
        }
    }
    if (providerListsValid_) {
        for (size_t index = 0U; index < safetyProviderCount_; ++index) {
            if (safetyProviders_[index] == nullptr) {
                providerListsValid_ = false;
                break;
            }
        }
    }
}

bool RuntimeStateCoordinator::isActive() const {
    return active_;
}

bool RuntimeStateCoordinator::isCompositionValid() const {
    return providerListsValid_;
}

RuntimeStateRefreshResult RuntimeStateCoordinator::refresh() {
    if (!active_ || status_ == nullptr) {
        return RuntimeStateRefreshResult::INACTIVE;
    }

    HealthState health = HealthState::FAULT;
    SafetyState safety = SafetyState::LOCKED;
    const bool valid = aggregate(health, safety);
    status_->health = health;
    status_->safety = safety;
    return valid
        ? RuntimeStateRefreshResult::REFRESHED
        : RuntimeStateRefreshResult::INVALID_PROVIDER;
}

bool RuntimeStateCoordinator::activate(RuntimeStatus& status) {
    if (active_) {
        return false;
    }

    HealthState health = HealthState::FAULT;
    SafetyState safety = SafetyState::LOCKED;
    const bool valid = aggregate(health, safety);
    if (!valid ||
        healthSeverity(health) < healthSeverity(status.health) ||
        (status.safety == SafetyState::LOCKED &&
         safety == SafetyState::CLEAR)) {
        return false;
    }

    status.health = health;
    status.safety = safety;
    status_ = &status;
    active_ = true;
    return true;
}

bool RuntimeStateCoordinator::aggregate(
    HealthState& health,
    SafetyState& safety
) const {
    if (!providerListsValid_) {
        health = HealthState::FAULT;
        safety = SafetyState::LOCKED;
        return false;
    }

    health = HealthState::OK;
    safety = SafetyState::CLEAR;

    for (size_t index = 0U; index < healthProviderCount_; ++index) {
        const HealthState contribution =
            healthProviders_[index]->healthContribution();
        if (!validHealth(contribution)) {
            health = HealthState::FAULT;
            safety = SafetyState::LOCKED;
            return false;
        }
        if (healthSeverity(contribution) > healthSeverity(health)) {
            health = contribution;
        }
    }

    for (size_t index = 0U; index < safetyProviderCount_; ++index) {
        const SafetyState contribution =
            safetyProviders_[index]->safetyContribution();
        if (!validSafety(contribution)) {
            health = HealthState::FAULT;
            safety = SafetyState::LOCKED;
            return false;
        }
        if (contribution == SafetyState::LOCKED) {
            safety = SafetyState::LOCKED;
        }
    }
    return true;
}

} // namespace System
} // namespace AquaCore
