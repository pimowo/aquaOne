#include <stdint.h>
#include <unity.h>

#include <AquaCore/Diagnostics/CoreDiagnosticProviders.h>
#include <AquaCore/Diagnostics/DiagnosticRegistry.h>

namespace {

using namespace AquaCore;
using namespace AquaCore::Diagnostics;
using namespace AquaCore::System;

// This descriptor belongs to the Application fixture, not to Core.
enum class ProjectDiagnosticId : uint8_t {
    Runtime = 9,
    Storage = 3,
    Domain = 7
};

struct ProjectDiagnosticEntry {
    ProjectDiagnosticId id;
};

StartupStepResult startupOk(void*) {
    return StartupStepResult::succeeded();
}

ApplicationPlan makePlan() {
    ApplicationPlan plan {};
    plan.earlySafeOutputs = {"safe-outputs", startupOk, nullptr};
    plan.safetyGate = {"safety-gate", startupOk, nullptr};
    return plan;
}

class StorageBackend final : public Config::StorageBackend {
public:
    bool begin(const char*) override { return true; }
    void end() override {}
    size_t blobLength(const char*) override { return 0U; }
    size_t readBlob(const char*, void*, size_t) override { return 0U; }
    size_t writeBlob(const char*, const void*, size_t) override { return 0U; }
};

struct DomainState {
    uint16_t sampleCount;
};

struct DomainSnapshot {
    uint16_t sampleCount;
};

class DomainDiagnosticProvider final : public DiagnosticProvider<DomainSnapshot> {
public:
    explicit DomainDiagnosticProvider(const DomainState& source) : source_(source) {}
    DomainDiagnosticProvider(DomainState&&) = delete;
    DomainDiagnosticProvider(const DomainState&&) = delete;

    DiagnosticReadResult read(DomainSnapshot& out) const override {
        out.sampleCount = source_.sampleCount;
        return DiagnosticReadResult::Success;
    }

private:
    const DomainState& source_;
};

void testProjectCompositionEnumeratesMetadataAndReadsProvidersSeparately() {
    ApplicationRuntime runtime(makePlan(), {nullptr, 0U});
    runtime.start();
    RuntimeStatusDiagnosticProvider runtimeProvider(runtime);

    StorageBackend backend;
    uint8_t workspace[16] {};
    Config::StorageService storage(
        backend, "composition-test", "a", "b", {workspace, sizeof(workspace)}
    );
    StorageStatusDiagnosticProvider storageProvider(storage);

    DomainState domainState {7U};
    DomainDiagnosticProvider domainProvider(domainState);

    const ProjectDiagnosticEntry entries[] = {
        {ProjectDiagnosticId::Runtime},
        {ProjectDiagnosticId::Storage},
        {ProjectDiagnosticId::Domain}
    };
    const ProjectDiagnosticId expected[] = {
        ProjectDiagnosticId::Runtime,
        ProjectDiagnosticId::Storage,
        ProjectDiagnosticId::Domain
    };

    {
        const DiagnosticRegistry<ProjectDiagnosticEntry> registry(entries, 3U);
        TEST_ASSERT_TRUE(registry.isValid());
        TEST_ASSERT_EQUAL_UINT32(3U, registry.size());
        for (size_t index = 0U; index < registry.size(); ++index) {
            TEST_ASSERT_EQUAL_PTR(&entries[index], registry.entryAt(index));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[index]),
                static_cast<int>(registry.entryAt(index)->id));
        }
    }

    // The registry has ended; typed providers still read their own sources.
    const DiagnosticProvider<RuntimeStatus>& runtimeRead = runtimeProvider;
    RuntimeStatus runtimeSnapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(runtimeRead.read(runtimeSnapshot)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OperationalState::RUNNING),
        static_cast<int>(runtimeSnapshot.operational));

    const DiagnosticProvider<Config::StorageStatus>& storageRead = storageProvider;
    Config::StorageStatus storageSnapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(storageRead.read(storageSnapshot)));
    TEST_ASSERT_FALSE(storageSnapshot.backendReady);

    const DiagnosticProvider<DomainSnapshot>& domainRead = domainProvider;
    DomainSnapshot domainSnapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(domainRead.read(domainSnapshot)));
    TEST_ASSERT_EQUAL_UINT16(7U, domainSnapshot.sampleCount);
    domainState.sampleCount = 8U;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(domainRead.read(domainSnapshot)));
    TEST_ASSERT_EQUAL_UINT16(8U, domainSnapshot.sampleCount);
}

} // namespace

void runDiagnosticRegistryCompositionTests() {
    RUN_TEST(testProjectCompositionEnumeratesMetadataAndReadsProvidersSeparately);
}
