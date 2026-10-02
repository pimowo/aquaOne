#include <unity.h>

#include <type_traits>

#include <AquaCore/Diagnostics/DiagnosticProvider.h>

using AquaCore::Diagnostics::DiagnosticProvider;
using AquaCore::Diagnostics::DiagnosticReadResult;

namespace {

struct FakeDomainState {
    bool diagnosticsAvailable;
    bool sensorReady;
    uint32_t operationCount;
    bool technicalFault;
};

struct FakeDomainDiagnosticsSnapshot {
    bool sensorReady;
    uint32_t operationCount;
    bool technicalFault;
};

struct OtherTypedSnapshot {
    uint8_t value;
};

class FakeDomainDiagnosticsProvider final
    : public DiagnosticProvider<FakeDomainDiagnosticsSnapshot> {
public:
    explicit FakeDomainDiagnosticsProvider(const FakeDomainState& source)
        : source_(source) {
    }

    FakeDomainDiagnosticsProvider(FakeDomainState&&) = delete;
    FakeDomainDiagnosticsProvider(const FakeDomainState&&) = delete;

    DiagnosticReadResult read(
        FakeDomainDiagnosticsSnapshot& out
    ) const override {
        if (!source_.diagnosticsAvailable) {
            return DiagnosticReadResult::Unavailable;
        }
        out.sensorReady = source_.sensorReady;
        out.operationCount = source_.operationCount;
        out.technicalFault = source_.technicalFault;
        return DiagnosticReadResult::Success;
    }

private:
    const FakeDomainState& source_;
};

static_assert(std::is_constructible<FakeDomainDiagnosticsProvider,
    const FakeDomainState&>::value, "Domain provider accepts lvalue source");
static_assert(!std::is_constructible<FakeDomainDiagnosticsProvider,
    FakeDomainState&&>::value, "Domain provider rejects temporary source");
static_assert(!std::is_constructible<FakeDomainDiagnosticsProvider,
    const FakeDomainState&&>::value, "Domain provider rejects const temporary");
static_assert(!std::is_convertible<DiagnosticProvider<
    FakeDomainDiagnosticsSnapshot>*, DiagnosticProvider<OtherTypedSnapshot>*>::value,
    "Domain snapshot types remain independent");

void testDomainProjectionReadsTypedCurrentFacts() {
    FakeDomainState state {true, true, 7U, false};
    FakeDomainDiagnosticsProvider provider(state);
    FakeDomainDiagnosticsSnapshot snapshot {};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_TRUE(snapshot.sensorReady);
    TEST_ASSERT_EQUAL_UINT32(7U, snapshot.operationCount);
    TEST_ASSERT_FALSE(snapshot.technicalFault);
}

void testDomainProjectionObservesLiveFactsAndFailureIsStillSuccess() {
    FakeDomainState state {true, true, 7U, false};
    FakeDomainDiagnosticsProvider provider(state);
    FakeDomainDiagnosticsSnapshot snapshot {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));

    state.operationCount = 8U;
    state.technicalFault = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Success),
        static_cast<int>(provider.read(snapshot)));
    TEST_ASSERT_EQUAL_UINT32(8U, snapshot.operationCount);
    TEST_ASSERT_TRUE(snapshot.technicalFault);
}

void testDomainUnavailableReadIsNotPublished() {
    FakeDomainState state {false, false, 0U, false};
    FakeDomainDiagnosticsProvider provider(state);
    FakeDomainDiagnosticsSnapshot snapshot {true, 12U, true};
    uint32_t publishedOperationCount = 99U;

    const DiagnosticReadResult result = provider.read(snapshot);
    if (result == DiagnosticReadResult::Success) {
        publishedOperationCount = snapshot.operationCount;
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagnosticReadResult::Unavailable),
        static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(99U, publishedOperationCount);
}

} // namespace

void runDomainDiagnosticProviderTests() {
    RUN_TEST(testDomainProjectionReadsTypedCurrentFacts);
    RUN_TEST(testDomainProjectionObservesLiveFactsAndFailureIsStillSuccess);
    RUN_TEST(testDomainUnavailableReadIsNotPublished);
}
