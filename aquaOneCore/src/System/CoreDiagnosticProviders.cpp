#include <AquaCore/Diagnostics/CoreDiagnosticProviders.h>

namespace AquaCore {
namespace Diagnostics {

RuntimeStatusDiagnosticProvider::RuntimeStatusDiagnosticProvider(
    const System::ApplicationRuntime& source
) : source_(source) {
}

DiagnosticReadResult RuntimeStatusDiagnosticProvider::read(
    System::RuntimeStatus& out
) const {
    out = source_.status();
    return DiagnosticReadResult::Success;
}

StartupReportDiagnosticProvider::StartupReportDiagnosticProvider(
    const System::ApplicationRuntime& source
) : source_(source) {
}

DiagnosticReadResult StartupReportDiagnosticProvider::read(
    StartupReportDiagnosticsSnapshot& out
) const {
    const System::StartupReport& report = source_.startupReport();
    if (!report.isComplete()) {
        out.report = nullptr;
        return DiagnosticReadResult::Unavailable;
    }
    out.report = &report;
    return DiagnosticReadResult::Success;
}

RuntimeIdentityDiagnosticProvider::RuntimeIdentityDiagnosticProvider(
    const Identity::RuntimeIdentityState& source
) : source_(source) {
}

DiagnosticReadResult RuntimeIdentityDiagnosticProvider::read(
    Identity::RuntimeIdentity& out
) const {
    const Identity::RuntimeIdentity identity = source_.identity();
    if (!identity.isValid()) {
        out = Identity::RuntimeIdentity();
        return DiagnosticReadResult::Unavailable;
    }
    out = identity;
    return DiagnosticReadResult::Success;
}

DeviceIdentityDiagnosticProvider::DeviceIdentityDiagnosticProvider(
    const SystemService& source
) : source_(source) {
}

DiagnosticReadResult DeviceIdentityDiagnosticProvider::read(
    AquaCore::DeviceIdentity& out
) const {
    if (!source_.isReady()) {
        return DiagnosticReadResult::Unavailable;
    }
    out = source_.deviceIdentity();
    return DiagnosticReadResult::Success;
}

ConfigLifecycleDiagnosticProvider::ConfigLifecycleDiagnosticProvider(
    const Config::ConfigLifecycle& source
) : source_(source) {
}

DiagnosticReadResult ConfigLifecycleDiagnosticProvider::read(
    Config::ConfigLifecycleStatus& out
) const {
    out = source_.status();
    return DiagnosticReadResult::Success;
}

StorageStatusDiagnosticProvider::StorageStatusDiagnosticProvider(
    const Config::StorageService& source
) : source_(source) {
}

DiagnosticReadResult StorageStatusDiagnosticProvider::read(
    Config::StorageStatus& out
) const {
    out = source_.status();
    return DiagnosticReadResult::Success;
}

} // namespace Diagnostics
} // namespace AquaCore
