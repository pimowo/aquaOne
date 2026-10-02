#include <unity.h>

#include <type_traits>

#include <AquaCore/Config/ConfigLifecycle.h>
#include <AquaCore/Diagnostics/CoreDiagnosticProviders.h>
#include <AquaCore/Maintenance/MaintenanceParticipant.h>
#include <AquaCore/System/RuntimeStateCoordinator.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace {

using namespace AquaCore;
using namespace AquaCore::Diagnostics;
using namespace AquaCore::System;

static_assert(std::is_constructible<RuntimeStatusDiagnosticProvider,
    const ApplicationRuntime&>::value, "Const lvalue source must be accepted");
static_assert(!std::is_constructible<RuntimeStatusDiagnosticProvider,
    ApplicationRuntime&&>::value, "Provider must reject temporary sources");
static_assert(std::is_constructible<StartupReportDiagnosticProvider,
    const ApplicationRuntime&>::value, "Const lvalue source must be accepted");
static_assert(!std::is_constructible<StartupReportDiagnosticProvider,
    const ApplicationRuntime&&>::value, "Provider must reject const temporaries");
static_assert(std::is_constructible<RuntimeIdentityDiagnosticProvider,
    const Identity::RuntimeIdentityState&>::value,
    "Const lvalue source must be accepted");
static_assert(!std::is_constructible<RuntimeIdentityDiagnosticProvider,
    Identity::RuntimeIdentityState&&>::value, "Provider must reject temporary sources");
static_assert(std::is_constructible<DeviceIdentityDiagnosticProvider,
    const SystemService&>::value, "Const lvalue source must be accepted");
static_assert(!std::is_constructible<DeviceIdentityDiagnosticProvider,
    SystemService&&>::value, "Provider must reject temporary sources");
static_assert(std::is_constructible<ConfigLifecycleDiagnosticProvider,
    const Config::ConfigLifecycle&>::value, "Const lvalue source must be accepted");
static_assert(!std::is_constructible<ConfigLifecycleDiagnosticProvider,
    Config::ConfigLifecycle&&>::value, "Provider must reject temporary sources");
static_assert(std::is_constructible<StorageStatusDiagnosticProvider,
    const Config::StorageService&>::value, "Const lvalue source must be accepted");
static_assert(!std::is_constructible<StorageStatusDiagnosticProvider,
    Config::StorageService&&>::value, "Provider must reject temporary sources");

StartupStepResult startupOk(void*) {
    return StartupStepResult::succeeded();
}

StartupStepResult startupFail(void*) {
    return StartupStepResult::failed(CoreStartupError::callbackMissing());
}

ApplicationPlan makePlan(StartupCallback safetyGate = startupOk) {
    ApplicationPlan plan {};
    plan.earlySafeOutputs = {"safe-outputs", startupOk, nullptr};
    plan.safetyGate = {"safety-gate", safetyGate, nullptr};
    plan.participants = nullptr;
    plan.participantCount = 0U;
    return plan;
}

class PreparedMaintenance final : public Maintenance::MaintenanceParticipant {
public:
    Maintenance::MaintenanceParticipantResult prepareEnter() override {
        return Maintenance::MaintenanceParticipantResult::Prepared;
    }
    Maintenance::MaintenanceParticipantResult prepareExit() override {
        return Maintenance::MaintenanceParticipantResult::Failed;
    }
};

class IdentityGenerator final : public Identity::RuntimeIdentityGenerator {
public:
    bool generate(Identity::RuntimeIdentity& output) override {
        return Identity::RuntimeIdentity::fromValue(value, output);
    }
    uint64_t value = 0xFEDCBA9876543210ULL;
};

class SystemBackend final : public AquaCore::SystemBackend {
public:
    uint32_t uptimeMs() const override { return 0U; }
    RestartReason restartReason() const override { return RestartReason::Unknown; }
};

class StorageBackend final : public Config::StorageBackend {
public:
    bool begin(const char*) override { return true; }
    void end() override {}
    size_t blobLength(const char*) override { return 0U; }
    size_t readBlob(const char*, void*, size_t) override { return 0U; }
    size_t writeBlob(const char*, const void*, size_t) override { return 0U; }
};

void testRuntimeStatusProjectionReadsLiveStateAndFailureIsSuccess() {
    ApplicationRuntime runtime(makePlan(), {nullptr, 0U});
    const StartupReport& report = runtime.start();
    RuntimeStatusDiagnosticProvider provider(runtime);
    RuntimeStatus snapshot {};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::RUNNING),
        static_cast<int>(snapshot.operational));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthState::OK),
        static_cast<int>(snapshot.health));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SafetyState::CLEAR),
        static_cast<int>(snapshot.safety));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StartupPhase::RUNNING),
        static_cast<int>(snapshot.startupPhase));

    RuntimeStateCoordinator coordinator(nullptr, 0U, nullptr, 0U);
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    PreparedMaintenance participant;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Maintenance::MaintenanceTransitionOutcome::Completed),
        static_cast<int>(runtime.requestMaintenanceTransition(
            Maintenance::MaintenanceTransitionRequest::Enter, participant)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::MAINTENANCE),
        static_cast<int>(snapshot.operational));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(Maintenance::MaintenanceTransitionOutcome::Failed),
        static_cast<int>(runtime.requestMaintenanceTransition(
            Maintenance::MaintenanceTransitionRequest::Exit, participant)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::ERROR),
        static_cast<int>(snapshot.operational));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthState::FAULT),
        static_cast<int>(snapshot.health));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SafetyState::LOCKED),
        static_cast<int>(snapshot.safety));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::RUNNING),
        static_cast<int>(report.finalStatus().operational));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthState::OK),
        static_cast<int>(report.finalStatus().health));
}

void testStartupProjectionUnavailableUntilFinalAndFailedStartupIsSuccess() {
    ApplicationRuntime incomplete(makePlan(), {nullptr, 0U});
    StartupReportDiagnosticProvider incompleteProvider(incomplete);
    StartupReportDiagnosticsSnapshot snapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Unavailable),
        static_cast<int>(incompleteProvider.read(snapshot)));
    TEST_ASSERT_NULL(snapshot.report);

    ApplicationRuntime failed(makePlan(startupFail), {nullptr, 0U});
    failed.start();
    StartupReportDiagnosticProvider provider(failed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_NOT_NULL(snapshot.report);
    TEST_ASSERT_TRUE(snapshot.report->isComplete());
    TEST_ASSERT_TRUE(snapshot.report->hasFatalFailure());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::ERROR),
        static_cast<int>(snapshot.report->finalStatus().operational));
}

void testIdentityProjectionUsesExistingOwnersAndRejectsInvalidIdentity() {
    Identity::RuntimeIdentityState state;
    RuntimeIdentityDiagnosticProvider runtimeProvider(state);
    Identity::RuntimeIdentity runtimeSnapshot;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Unavailable),
        static_cast<int>(runtimeProvider.read(runtimeSnapshot)));
    IdentityGenerator generator;
    TEST_ASSERT_TRUE(state.initialize(generator));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(runtimeProvider.read(runtimeSnapshot)));
    TEST_ASSERT_EQUAL_UINT64(generator.value, runtimeSnapshot.value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(runtimeProvider.read(runtimeSnapshot)));
    TEST_ASSERT_EQUAL_UINT64(generator.value, runtimeSnapshot.value());

    SystemBackend backend;
    SystemService system(backend);
    DeviceIdentityDiagnosticProvider deviceProvider(system);
    AquaCore::DeviceIdentity deviceSnapshot;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Unavailable),
        static_cast<int>(deviceProvider.read(deviceSnapshot)));
    const AquaCore::DeviceIdentity identity(
        "controller", "unit", "1.0", "board"
    );
    TEST_ASSERT_TRUE(system.begin(identity));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(deviceProvider.read(deviceSnapshot)));
    TEST_ASSERT_EQUAL_STRING("controller", deviceSnapshot.deviceType);
    TEST_ASSERT_EQUAL_STRING("unit", deviceSnapshot.deviceName);
}

void testConfigAndStorageExposeOnlyExistingTypedStatuses() {
    Config::ConfigLifecycle lifecycle(
        {}, {}, {}, {}
    );
    ConfigLifecycleDiagnosticProvider configProvider(lifecycle);
    Config::ConfigLifecycleStatus configSnapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(configProvider.read(configSnapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Config::ConfigOperationResult::NotAttempted),
        static_cast<int>(configSnapshot.lastResult));
    TEST_ASSERT_FALSE(configSnapshot.restartRequired);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Config::ConfigOperationResult::InvalidArgument),
        static_cast<int>(lifecycle.startup()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(configProvider.read(configSnapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Config::ConfigOperationResult::InvalidArgument),
        static_cast<int>(configSnapshot.lastResult));

    StorageBackend backend;
    uint8_t workspace[16] {};
    Config::StorageService storage(
        backend, "diagnostic-test", "a", "b", {workspace, sizeof(workspace)}
    );
    StorageStatusDiagnosticProvider storageProvider(storage);
    Config::StorageStatus storageSnapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(storageProvider.read(storageSnapshot)));
    TEST_ASSERT_FALSE(storageSnapshot.backendReady);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Config::StorageOperationResult::NotAttempted),
        static_cast<int>(storageSnapshot.lastLoadResult));
}

} // namespace

void runCoreDiagnosticProviderTests() {
    RUN_TEST(testRuntimeStatusProjectionReadsLiveStateAndFailureIsSuccess);
    RUN_TEST(testStartupProjectionUnavailableUntilFinalAndFailedStartupIsSuccess);
    RUN_TEST(testIdentityProjectionUsesExistingOwnersAndRejectsInvalidIdentity);
    RUN_TEST(testConfigAndStorageExposeOnlyExistingTypedStatuses);
}
