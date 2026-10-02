#pragma once

#include <AquaCore/Config/ConfigLifecycle.h>
#include <AquaCore/Config/StorageService.h>
#include <AquaCore/Diagnostics/DiagnosticProvider.h>
#include <AquaCore/System/ApplicationRuntime.h>
#include <AquaCore/System/RuntimeIdentity.h>
#include <AquaCore/System/SystemService.h>

namespace AquaCore {
namespace Diagnostics {

// Each provider borrows only its authoritative source. Source lifetime must
// cover provider lifetime, and provider lifetime must cover consumer use.
class RuntimeStatusDiagnosticProvider final
    : public DiagnosticProvider<System::RuntimeStatus> {
public:
    explicit RuntimeStatusDiagnosticProvider(
        const System::ApplicationRuntime& source
    );
    RuntimeStatusDiagnosticProvider(System::ApplicationRuntime&&) = delete;
    RuntimeStatusDiagnosticProvider(const System::ApplicationRuntime&&) = delete;
    DiagnosticReadResult read(System::RuntimeStatus& out) const override;

private:
    const System::ApplicationRuntime& source_;
};

struct StartupReportDiagnosticsSnapshot {
    // Non-null only for Success. StartupReport is immutable after completion;
    // this view avoids copying or redefining its bounded failure records.
    const System::StartupReport* report = nullptr;
};

class StartupReportDiagnosticProvider final
    : public DiagnosticProvider<StartupReportDiagnosticsSnapshot> {
public:
    explicit StartupReportDiagnosticProvider(
        const System::ApplicationRuntime& source
    );
    StartupReportDiagnosticProvider(System::ApplicationRuntime&&) = delete;
    StartupReportDiagnosticProvider(const System::ApplicationRuntime&&) = delete;
    DiagnosticReadResult read(
        StartupReportDiagnosticsSnapshot& out
    ) const override;

private:
    const System::ApplicationRuntime& source_;
};

class RuntimeIdentityDiagnosticProvider final
    : public DiagnosticProvider<Identity::RuntimeIdentity> {
public:
    explicit RuntimeIdentityDiagnosticProvider(
        const Identity::RuntimeIdentityState& source
    );
    RuntimeIdentityDiagnosticProvider(Identity::RuntimeIdentityState&&) = delete;
    RuntimeIdentityDiagnosticProvider(const Identity::RuntimeIdentityState&&) = delete;
    DiagnosticReadResult read(Identity::RuntimeIdentity& out) const override;

private:
    const Identity::RuntimeIdentityState& source_;
};

class DeviceIdentityDiagnosticProvider final
    : public DiagnosticProvider<AquaCore::DeviceIdentity> {
public:
    explicit DeviceIdentityDiagnosticProvider(const SystemService& source);
    DeviceIdentityDiagnosticProvider(SystemService&&) = delete;
    DeviceIdentityDiagnosticProvider(const SystemService&&) = delete;
    DiagnosticReadResult read(AquaCore::DeviceIdentity& out) const override;

private:
    const SystemService& source_;
};

class ConfigLifecycleDiagnosticProvider final
    : public DiagnosticProvider<Config::ConfigLifecycleStatus> {
public:
    explicit ConfigLifecycleDiagnosticProvider(
        const Config::ConfigLifecycle& source
    );
    ConfigLifecycleDiagnosticProvider(Config::ConfigLifecycle&&) = delete;
    ConfigLifecycleDiagnosticProvider(const Config::ConfigLifecycle&&) = delete;
    DiagnosticReadResult read(Config::ConfigLifecycleStatus& out) const override;

private:
    const Config::ConfigLifecycle& source_;
};

class StorageStatusDiagnosticProvider final
    : public DiagnosticProvider<Config::StorageStatus> {
public:
    explicit StorageStatusDiagnosticProvider(const Config::StorageService& source);
    StorageStatusDiagnosticProvider(Config::StorageService&&) = delete;
    StorageStatusDiagnosticProvider(const Config::StorageService&&) = delete;
    DiagnosticReadResult read(Config::StorageStatus& out) const override;

private:
    const Config::StorageService& source_;
};

} // namespace Diagnostics
} // namespace AquaCore
