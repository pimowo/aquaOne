#pragma once

#include "AquaCore/Alarms/AlarmState.h"
#include "AquaCore/System/RuntimeStateCoordinator.h"

namespace AquaCore {
namespace Alarms {

// Policy and optional context belong to the project/Application composition.
// A null context is valid for a stateless policy. All borrowed inputs must
// outlive the provider and coordinator that pulls it.
template <typename AlarmId>
using AlarmHealthPolicy = System::HealthState (*)(
    const AlarmSnapshot<AlarmId>&, const void* context);

template <typename AlarmId>
using AlarmSafetyPolicy = System::SafetyState (*)(
    const AlarmSnapshot<AlarmId>&, const void* context);

template <typename AlarmId>
class AlarmHealthProvider final : public System::HealthProvider {
public:
    AlarmHealthProvider(const AlarmState<AlarmId>& alarm,
                        AlarmHealthPolicy<AlarmId> policy,
                        const void* context = nullptr)
        : alarm_(alarm), policy_(policy), context_(context) {}

    AlarmHealthProvider(const AlarmHealthProvider&) = delete;
    AlarmHealthProvider& operator=(const AlarmHealthProvider&) = delete;

    System::HealthState healthContribution() const override {
        if (policy_ == nullptr) {
            return System::HealthState::FAULT;
        }
        const System::HealthState value = policy_(alarm_.snapshot(), context_);
        switch (value) {
            case System::HealthState::OK:
            case System::HealthState::DEGRADED:
            case System::HealthState::FAULT:
                return value;
            default:
                return System::HealthState::FAULT;
        }
    }

private:
    const AlarmState<AlarmId>& alarm_;
    const AlarmHealthPolicy<AlarmId> policy_;
    const void* const context_;
};

template <typename AlarmId>
class AlarmSafetyProvider final : public System::SafetyProvider {
public:
    AlarmSafetyProvider(const AlarmState<AlarmId>& alarm,
                        AlarmSafetyPolicy<AlarmId> policy,
                        const void* context = nullptr)
        : alarm_(alarm), policy_(policy), context_(context) {}

    AlarmSafetyProvider(const AlarmSafetyProvider&) = delete;
    AlarmSafetyProvider& operator=(const AlarmSafetyProvider&) = delete;

    System::SafetyState safetyContribution() const override {
        if (policy_ == nullptr) {
            return System::SafetyState::LOCKED;
        }
        const System::SafetyState value = policy_(alarm_.snapshot(), context_);
        switch (value) {
            case System::SafetyState::CLEAR:
            case System::SafetyState::LOCKED:
                return value;
            default:
                return System::SafetyState::LOCKED;
        }
    }

private:
    const AlarmState<AlarmId>& alarm_;
    const AlarmSafetyPolicy<AlarmId> policy_;
    const void* const context_;
};

} // namespace Alarms
} // namespace AquaCore
