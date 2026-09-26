#pragma once

#include <stddef.h>
#include <stdint.h>

#include <AquaCore/System/Startup.h>

namespace AquaCore {
namespace System {

class ApplicationRuntime;

class HealthProvider {
public:
    virtual HealthState healthContribution() const = 0;

protected:
    ~HealthProvider() = default;
};

class SafetyProvider {
public:
    virtual SafetyState safetyContribution() const = 0;

protected:
    ~SafetyProvider() = default;
};

enum class RuntimeStateRefreshResult : uint8_t {
    REFRESHED,
    INACTIVE,
    INVALID_PROVIDER
};

class RuntimeStateCoordinator {
public:
    // Providers and pointer arrays are borrowed, immutable composition.
    // Their lifetime must cover this coordinator. After handoff, the
    // ApplicationRuntime storage must cover every coordinator use.
    RuntimeStateCoordinator(
        const HealthProvider* const* healthProviders,
        size_t healthProviderCount,
        const SafetyProvider* const* safetyProviders,
        size_t safetyProviderCount
    );

    RuntimeStateCoordinator(const RuntimeStateCoordinator&) = delete;
    RuntimeStateCoordinator& operator=(const RuntimeStateCoordinator&) = delete;
    RuntimeStateCoordinator(RuntimeStateCoordinator&&) = delete;
    RuntimeStateCoordinator& operator=(RuntimeStateCoordinator&&) = delete;

    bool isActive() const;
    bool isCompositionValid() const;
    // Pulls every provider and publishes one complete Health/Safety pair.
    RuntimeStateRefreshResult refresh();

private:
    bool activate(RuntimeStatus& status);
    bool aggregate(
        HealthState& health,
        SafetyState& safety
    ) const;

    const HealthProvider* const* healthProviders_;
    size_t healthProviderCount_;
    const SafetyProvider* const* safetyProviders_;
    size_t safetyProviderCount_;
    RuntimeStatus* status_;
    bool providerListsValid_;
    bool active_;

    friend class ApplicationRuntime;
};

} // namespace System
} // namespace AquaCore
