#include <type_traits>
#include <unity.h>

#include "AquaCore/Alarms/AlarmEvent.h"
#include "AquaCore/Alarms/AlarmProviders.h"
#include "AquaCore/Events/EventSink.h"
#include "AquaCore/System/ApplicationRuntime.h"

namespace {

namespace Alarms = AquaCore::Alarms;
namespace Events = AquaCore::Events;
namespace System = AquaCore::System;

enum class TemperatureAlarm { High };
enum class LeakAlarm { Detected };

static_assert(!std::is_convertible<
    Events::EventSink<Alarms::AlarmEvent<TemperatureAlarm>>*,
    Events::EventSink<Alarms::AlarmEvent<LeakAlarm>>*>::value,
    "Alarm events keep Domain ID types separate");

template <typename AlarmId>
class RecordingSink final : public Events::EventSink<Alarms::AlarmEvent<AlarmId>> {
public:
    explicit RecordingSink(AlarmId id)
        : calls(0U), last{Alarms::AlarmEventKind::Activated,
                          {id, false, false, false, false}} {}

    void emit(const Alarms::AlarmEvent<AlarmId>& event) override {
        ++calls;
        last = event;
    }

    unsigned calls;
    Alarms::AlarmEvent<AlarmId> last;
};

template <typename AlarmId>
void emitAfter(Alarms::AlarmEventEmitter<AlarmId>& emitter,
               Alarms::AlarmState<AlarmId>& alarm,
               Alarms::AlarmTransitionResult result) {
    emitter.emitTransition(result, alarm.snapshot());
}

void testActivationDuplicateAndAckEvents() {
    Alarms::AlarmState<TemperatureAlarm> alarm(TemperatureAlarm::High, false);
    RecordingSink<TemperatureAlarm> sink(TemperatureAlarm::High);
    Alarms::AlarmEventEmitter<TemperatureAlarm> emitter(sink);
    emitAfter(emitter, alarm, alarm.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(1U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Activated, sink.last.kind);
    TEST_ASSERT_EQUAL(TemperatureAlarm::High, sink.last.snapshot.id);
    TEST_ASSERT_TRUE(sink.last.snapshot.conditionActive);
    TEST_ASSERT_TRUE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.acknowledged);

    emitAfter(emitter, alarm, alarm.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(1U, sink.calls);
    emitAfter(emitter, alarm, alarm.acknowledge());
    TEST_ASSERT_EQUAL_UINT(2U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Acknowledged, sink.last.kind);
    TEST_ASSERT_TRUE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_TRUE(sink.last.snapshot.acknowledged);
    emitAfter(emitter, alarm, alarm.acknowledge());
    TEST_ASSERT_EQUAL_UINT(2U, sink.calls);
}

void testNonLatchedRecoveryEmitsCleared() {
    Alarms::AlarmState<TemperatureAlarm> alarm(TemperatureAlarm::High, false);
    RecordingSink<TemperatureAlarm> sink(TemperatureAlarm::High);
    Alarms::AlarmEventEmitter<TemperatureAlarm> emitter(sink);
    alarm.setCondition(true);
    emitAfter(emitter, alarm, alarm.setCondition(false));
    TEST_ASSERT_EQUAL_UINT(1U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Cleared, sink.last.kind);
    TEST_ASSERT_FALSE(sink.last.snapshot.conditionActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.acknowledged);
}

void testLatchedRecoveryReoccurrenceAndExplicitClearEvents() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    RecordingSink<LeakAlarm> sink(LeakAlarm::Detected);
    Alarms::AlarmEventEmitter<LeakAlarm> emitter(sink);
    emitAfter(emitter, alarm, alarm.setCondition(true));
    emitAfter(emitter, alarm, alarm.acknowledge());
    emitAfter(emitter, alarm, alarm.setCondition(false));
    TEST_ASSERT_EQUAL_UINT(3U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::ConditionRecovered, sink.last.kind);
    TEST_ASSERT_FALSE(sink.last.snapshot.conditionActive);
    TEST_ASSERT_TRUE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_TRUE(sink.last.snapshot.acknowledged);

    emitAfter(emitter, alarm, alarm.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(4U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Reoccurred, sink.last.kind);
    TEST_ASSERT_TRUE(sink.last.snapshot.conditionActive);
    TEST_ASSERT_TRUE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.acknowledged);
    emitAfter(emitter, alarm, alarm.clearLatched());
    TEST_ASSERT_EQUAL_UINT(4U, sink.calls);
    emitAfter(emitter, alarm, alarm.setCondition(false));
    emitAfter(emitter, alarm, alarm.clearLatched());
    TEST_ASSERT_EQUAL_UINT(6U, sink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Cleared, sink.last.kind);
    TEST_ASSERT_FALSE(sink.last.snapshot.conditionActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.alarmActive);
    TEST_ASSERT_FALSE(sink.last.snapshot.acknowledged);
}

void testRestoreAndFirstConditionConfirmationDoNotInventOccurrence() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    RecordingSink<LeakAlarm> sink(LeakAlarm::Detected);
    Alarms::AlarmEventEmitter<LeakAlarm> emitter(sink);
    TEST_ASSERT_EQUAL(Alarms::AlarmRestoreResult::Restored,
                      alarm.restore(Alarms::AlarmPersistentState(true)));
    TEST_ASSERT_TRUE(alarm.snapshot().alarmActive);
    TEST_ASSERT_EQUAL_UINT(0U, sink.calls);
    const Alarms::AlarmTransitionResult first = alarm.setCondition(true);
    TEST_ASSERT_EQUAL(Alarms::AlarmTransitionResult::ConditionConfirmed, first);
    TEST_ASSERT_TRUE(alarm.snapshot().conditionActive);
    TEST_ASSERT_TRUE(alarm.snapshot().alarmActive);
    TEST_ASSERT_FALSE(alarm.snapshot().acknowledged);
    emitAfter(emitter, alarm, first);
    TEST_ASSERT_EQUAL_UINT(0U, sink.calls);
    emitAfter(emitter, alarm, alarm.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(0U, sink.calls);

    Alarms::AlarmState<LeakAlarm> recovered(LeakAlarm::Detected, true);
    RecordingSink<LeakAlarm> recoveredSink(LeakAlarm::Detected);
    Alarms::AlarmEventEmitter<LeakAlarm> recoveredEmitter(recoveredSink);
    TEST_ASSERT_EQUAL(Alarms::AlarmRestoreResult::Restored,
                      recovered.restore(Alarms::AlarmPersistentState(true)));
    const Alarms::AlarmTransitionResult firstRecovered = recovered.setCondition(false);
    TEST_ASSERT_EQUAL(Alarms::AlarmTransitionResult::NoChange, firstRecovered);
    emitAfter(recoveredEmitter, recovered, firstRecovered);
    TEST_ASSERT_EQUAL_UINT(0U, recoveredSink.calls);
    TEST_ASSERT_FALSE(recovered.snapshot().conditionActive);
    TEST_ASSERT_TRUE(recovered.snapshot().alarmActive);
    TEST_ASSERT_FALSE(recovered.snapshot().acknowledged);
    emitAfter(recoveredEmitter, recovered, recovered.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(1U, recoveredSink.calls);
    TEST_ASSERT_EQUAL(Alarms::AlarmEventKind::Reoccurred, recoveredSink.last.kind);
    TEST_ASSERT_TRUE(recoveredSink.last.snapshot.conditionActive);
    TEST_ASSERT_TRUE(recoveredSink.last.snapshot.alarmActive);
    TEST_ASSERT_FALSE(recoveredSink.last.snapshot.acknowledged);
}

void testTwoTypedAlarmSinksAreIndependent() {
    Alarms::AlarmState<TemperatureAlarm> temperature(TemperatureAlarm::High, false);
    Alarms::AlarmState<LeakAlarm> leak(LeakAlarm::Detected, true);
    RecordingSink<TemperatureAlarm> temperatureSink(TemperatureAlarm::High);
    RecordingSink<LeakAlarm> leakSink(LeakAlarm::Detected);
    Alarms::AlarmEventEmitter<TemperatureAlarm> temperatureEmitter(temperatureSink);
    Alarms::AlarmEventEmitter<LeakAlarm> leakEmitter(leakSink);
    emitAfter(temperatureEmitter, temperature, temperature.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(1U, temperatureSink.calls);
    TEST_ASSERT_EQUAL_UINT(0U, leakSink.calls);
    emitAfter(leakEmitter, leak, leak.setCondition(true));
    TEST_ASSERT_EQUAL_UINT(1U, temperatureSink.calls);
    TEST_ASSERT_EQUAL_UINT(1U, leakSink.calls);
}

void testDiscardSinkCannotRollbackAlarmTransition() {
    Alarms::AlarmState<TemperatureAlarm> alarm(TemperatureAlarm::High, false);
    Events::NullEventSink<Alarms::AlarmEvent<TemperatureAlarm>> sink;
    Alarms::AlarmEventEmitter<TemperatureAlarm> emitter(sink);
    emitAfter(emitter, alarm, alarm.setCondition(true));
    TEST_ASSERT_TRUE(alarm.snapshot().alarmActive);
}

struct Mapping {
    System::HealthState activeHealth;
    System::SafetyState activeSafety;
    bool useCondition;
};

template <typename AlarmId>
System::HealthState mapHealth(const Alarms::AlarmSnapshot<AlarmId>& alarm,
                              const void* context) {
    const Mapping& mapping = *static_cast<const Mapping*>(context);
    const bool selected = mapping.useCondition
        ? alarm.conditionActive : alarm.alarmActive;
    return selected ? mapping.activeHealth : System::HealthState::OK;
}

template <typename AlarmId>
System::SafetyState mapSafety(const Alarms::AlarmSnapshot<AlarmId>& alarm,
                              const void* context) {
    const Mapping& mapping = *static_cast<const Mapping*>(context);
    const bool selected = mapping.useCondition
        ? alarm.conditionActive : alarm.alarmActive;
    return selected ? mapping.activeSafety : System::SafetyState::CLEAR;
}

template <typename AlarmId>
System::HealthState neutralHealth(const Alarms::AlarmSnapshot<AlarmId>&,
                                  const void*) {
    return System::HealthState::OK;
}

template <typename AlarmId>
System::SafetyState neutralSafety(const Alarms::AlarmSnapshot<AlarmId>&,
                                  const void*) {
    return System::SafetyState::CLEAR;
}

template <typename AlarmId>
System::HealthState invalidHealth(const Alarms::AlarmSnapshot<AlarmId>&,
                                  const void*) {
    return static_cast<System::HealthState>(0xFFU);
}

template <typename AlarmId>
System::SafetyState invalidSafety(const Alarms::AlarmSnapshot<AlarmId>&,
                                  const void*) {
    return static_cast<System::SafetyState>(0xFFU);
}

void testProviderPullSeesAckLatchRecoveryAndClear() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    const Mapping mapping = {System::HealthState::FAULT,
                             System::SafetyState::LOCKED, false};
    Alarms::AlarmHealthProvider<LeakAlarm> health(alarm, mapHealth<LeakAlarm>, &mapping);
    Alarms::AlarmSafetyProvider<LeakAlarm> safety(alarm, mapSafety<LeakAlarm>, &mapping);
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
    alarm.setCondition(true);
    TEST_ASSERT_EQUAL(System::HealthState::FAULT, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, safety.safetyContribution());
    alarm.acknowledge();
    TEST_ASSERT_EQUAL(System::HealthState::FAULT, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, safety.safetyContribution());
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(System::HealthState::FAULT, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, safety.safetyContribution());
    alarm.clearLatched();
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
}

void testNonLatchedRecoveryAndNeutralPolicy() {
    Alarms::AlarmState<TemperatureAlarm> alarm(TemperatureAlarm::High, false);
    const Mapping mapping = {System::HealthState::DEGRADED,
                             System::SafetyState::LOCKED, false};
    Alarms::AlarmHealthProvider<TemperatureAlarm> health(
        alarm, mapHealth<TemperatureAlarm>, &mapping);
    Alarms::AlarmSafetyProvider<TemperatureAlarm> safety(
        alarm, neutralSafety<TemperatureAlarm>);
    alarm.setCondition(true);
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
}

void testPolicyMayUseCurrentConditionInsteadOfLatchedActivity() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    const Mapping mapping = {System::HealthState::DEGRADED,
                             System::SafetyState::LOCKED, true};
    Alarms::AlarmHealthProvider<LeakAlarm> health(alarm, mapHealth<LeakAlarm>, &mapping);
    Alarms::AlarmSafetyProvider<LeakAlarm> safety(alarm, mapSafety<LeakAlarm>, &mapping);
    alarm.setCondition(true);
    TEST_ASSERT_EQUAL(System::HealthState::DEGRADED, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, safety.safetyContribution());
    alarm.setCondition(false);
    TEST_ASSERT_TRUE(alarm.snapshot().alarmActive);
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
}

void testMissingOrInvalidPolicyFailsSafe() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    Alarms::AlarmHealthProvider<LeakAlarm> missingHealth(alarm, nullptr);
    Alarms::AlarmSafetyProvider<LeakAlarm> missingSafety(alarm, nullptr);
    Alarms::AlarmHealthProvider<LeakAlarm> badHealth(alarm, invalidHealth<LeakAlarm>);
    Alarms::AlarmSafetyProvider<LeakAlarm> badSafety(alarm, invalidSafety<LeakAlarm>);
    TEST_ASSERT_EQUAL(System::HealthState::FAULT, missingHealth.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, missingSafety.safetyContribution());
    TEST_ASSERT_EQUAL(System::HealthState::FAULT, badHealth.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::LOCKED, badSafety.safetyContribution());
}

System::StartupStepResult successfulStartupStep(void*) {
    return System::StartupStepResult::succeeded();
}

System::ApplicationPlan minimalStartupPlan() {
    const System::StartupAction early = {"early", successfulStartupStep, nullptr};
    const System::StartupAction gate = {"gate", successfulStartupStep, nullptr};
    return {early, gate, nullptr, 0U};
}

void assertRuntimeState(const System::ApplicationRuntime& runtime,
                        System::HealthState health,
                        System::SafetyState safety) {
    const System::RuntimeStatus status = runtime.status();
    TEST_ASSERT_EQUAL(System::OperationalState::RUNNING, status.operational);
    TEST_ASSERT_EQUAL(health, status.health);
    TEST_ASSERT_EQUAL(safety, status.safety);
}

void testCoordinatorAggregatesDifferentAlarmPolicies() {
    Alarms::AlarmState<TemperatureAlarm> temperature(TemperatureAlarm::High, false);
    Alarms::AlarmState<LeakAlarm> leak(LeakAlarm::Detected, true);
    const Mapping temperatureMapping = {System::HealthState::DEGRADED,
                                        System::SafetyState::CLEAR, false};
    const Mapping leakMapping = {System::HealthState::FAULT,
                                 System::SafetyState::LOCKED, false};
    Alarms::AlarmHealthProvider<TemperatureAlarm> temperatureHealth(
        temperature, mapHealth<TemperatureAlarm>, &temperatureMapping);
    Alarms::AlarmSafetyProvider<TemperatureAlarm> temperatureSafety(
        temperature, mapSafety<TemperatureAlarm>, &temperatureMapping);
    Alarms::AlarmHealthProvider<LeakAlarm> leakHealth(
        leak, mapHealth<LeakAlarm>, &leakMapping);
    Alarms::AlarmSafetyProvider<LeakAlarm> leakSafety(
        leak, mapSafety<LeakAlarm>, &leakMapping);
    const System::HealthProvider* healthProviders[] = {&temperatureHealth, &leakHealth};
    const System::SafetyProvider* safetyProviders[] = {&temperatureSafety, &leakSafety};
    System::RuntimeStateCoordinator coordinator(healthProviders, 2U, safetyProviders, 2U);
    System::ApplicationRuntime runtime(minimalStartupPlan(), {nullptr, 0U});
    runtime.start();
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    temperature.setCondition(true);
    leak.setCondition(true);
    TEST_ASSERT_EQUAL(System::RuntimeStateRefreshResult::REFRESHED,
                      coordinator.refresh());
    assertRuntimeState(runtime, System::HealthState::FAULT, System::SafetyState::LOCKED);

    leak.acknowledge();
    coordinator.refresh();
    assertRuntimeState(runtime, System::HealthState::FAULT, System::SafetyState::LOCKED);
    leak.setCondition(false);
    coordinator.refresh();
    assertRuntimeState(runtime, System::HealthState::FAULT, System::SafetyState::LOCKED);
    leak.clearLatched();
    coordinator.refresh();
    assertRuntimeState(runtime, System::HealthState::DEGRADED, System::SafetyState::CLEAR);
    temperature.setCondition(false);
    coordinator.refresh();
    assertRuntimeState(runtime, System::HealthState::OK, System::SafetyState::CLEAR);
}

void testRestoredLatchContributesBeforeLiveConditionEvaluation() {
    Alarms::AlarmState<LeakAlarm> leak(LeakAlarm::Detected, true);
    TEST_ASSERT_EQUAL(Alarms::AlarmRestoreResult::Restored,
                      leak.restore(Alarms::AlarmPersistentState(true)));
    const Mapping mapping = {System::HealthState::FAULT,
                             System::SafetyState::LOCKED, false};
    Alarms::AlarmHealthProvider<LeakAlarm> health(leak, mapHealth<LeakAlarm>, &mapping);
    Alarms::AlarmSafetyProvider<LeakAlarm> safety(leak, mapSafety<LeakAlarm>, &mapping);
    const System::HealthProvider* healthProviders[] = {&health};
    const System::SafetyProvider* safetyProviders[] = {&safety};
    System::RuntimeStateCoordinator coordinator(healthProviders, 1U, safetyProviders, 1U);
    System::ApplicationRuntime runtime(minimalStartupPlan(), {nullptr, 0U});
    runtime.start();
    TEST_ASSERT_TRUE(runtime.handoffRuntimeState(coordinator));
    assertRuntimeState(runtime, System::HealthState::FAULT, System::SafetyState::LOCKED);
    TEST_ASSERT_EQUAL(Alarms::AlarmTransitionResult::NoChange, leak.clearLatched());
    leak.setCondition(false);
    TEST_ASSERT_TRUE(leak.snapshot().alarmActive);
    leak.clearLatched();
    coordinator.refresh();
    assertRuntimeState(runtime, System::HealthState::OK, System::SafetyState::CLEAR);
}

void testActiveAlarmCanRemainNeutralByExplicitPolicy() {
    Alarms::AlarmState<LeakAlarm> alarm(LeakAlarm::Detected, true);
    Alarms::AlarmHealthProvider<LeakAlarm> health(alarm, neutralHealth<LeakAlarm>);
    Alarms::AlarmSafetyProvider<LeakAlarm> safety(alarm, neutralSafety<LeakAlarm>);
    alarm.setCondition(true);
    TEST_ASSERT_EQUAL(System::HealthState::OK, health.healthContribution());
    TEST_ASSERT_EQUAL(System::SafetyState::CLEAR, safety.safetyContribution());
}

} // namespace

void runAlarmIntegrationTests() {
    RUN_TEST(testActivationDuplicateAndAckEvents);
    RUN_TEST(testNonLatchedRecoveryEmitsCleared);
    RUN_TEST(testLatchedRecoveryReoccurrenceAndExplicitClearEvents);
    RUN_TEST(testRestoreAndFirstConditionConfirmationDoNotInventOccurrence);
    RUN_TEST(testTwoTypedAlarmSinksAreIndependent);
    RUN_TEST(testDiscardSinkCannotRollbackAlarmTransition);
    RUN_TEST(testProviderPullSeesAckLatchRecoveryAndClear);
    RUN_TEST(testNonLatchedRecoveryAndNeutralPolicy);
    RUN_TEST(testPolicyMayUseCurrentConditionInsteadOfLatchedActivity);
    RUN_TEST(testMissingOrInvalidPolicyFailsSafe);
    RUN_TEST(testCoordinatorAggregatesDifferentAlarmPolicies);
    RUN_TEST(testRestoredLatchContributesBeforeLiveConditionEvaluation);
    RUN_TEST(testActiveAlarmCanRemainNeutralByExplicitPolicy);
}
