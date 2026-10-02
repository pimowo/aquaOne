#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <type_traits>
#include <utility>
#include <unity.h>

#include "AquaCore/Alarms/AlarmControl.h"
#include "AquaCore/Alarms/AlarmState.h"

void runAlarmIntegrationTests();

namespace {

using AquaCore::Alarms::AlarmControl;
using AquaCore::Alarms::AlarmPersistentState;
using AquaCore::Alarms::AlarmRestoreResult;
using AquaCore::Alarms::AlarmSnapshot;
using AquaCore::Alarms::AlarmState;
using AquaCore::Alarms::AlarmTransitionResult;

enum class DomainAlarm { WaterLevel, Temperature };
enum class OtherDomainAlarm { Pump };

template <typename T>
class HasSetCondition {
    template <typename U>
    static auto probe(int) -> decltype(std::declval<U&>().setCondition(true),
                                       std::true_type());
    template <typename>
    static std::false_type probe(...);
public:
    static const bool value = decltype(probe<T>(0))::value;
};

static_assert(!std::is_copy_constructible<AlarmState<DomainAlarm>>::value,
              "Alarm owner must not be copied");
static_assert(!std::is_convertible<AlarmState<DomainAlarm>*,
                                   AlarmState<OtherDomainAlarm>*>::value,
              "Domain alarm ID types must remain separate");
static_assert(!HasSetCondition<AlarmControl<DomainAlarm>>::value,
              "Application control must not mutate Domain condition");

void assertState(const AlarmState<DomainAlarm>& alarm, DomainAlarm id,
                 bool condition, bool active, bool acknowledged, bool latched) {
    const AlarmSnapshot<DomainAlarm> snapshot = alarm.snapshot();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(id), static_cast<int>(snapshot.id));
    TEST_ASSERT_EQUAL(condition, snapshot.conditionActive);
    TEST_ASSERT_EQUAL(active, snapshot.alarmActive);
    TEST_ASSERT_EQUAL(acknowledged, snapshot.acknowledged);
    TEST_ASSERT_EQUAL(latched, snapshot.latched);
}

void testInitialStateAndInactiveOperations() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, false);
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.acknowledge());
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(false));
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, false);
}

void testNonLatchedActivationAckAndRecovery() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Activated, alarm.setCondition(true));
    assertState(alarm, DomainAlarm::Temperature, true, true, false, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, alarm.acknowledge());
    assertState(alarm, DomainAlarm::Temperature, true, true, true, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Cleared, alarm.setCondition(false));
    assertState(alarm, DomainAlarm::Temperature, false, false, false, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Activated, alarm.setCondition(true));
    assertState(alarm, DomainAlarm::Temperature, true, true, false, false);
}

void testNonLatchedDuplicateSamplesDoNotResetAck() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, false);
    alarm.setCondition(true);
    alarm.acknowledge();
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(true));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.acknowledge());
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
    assertState(alarm, DomainAlarm::WaterLevel, true, true, true, false);
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(false));
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, false);
}

void testLatchedRecoveryRequiresExplicitClear() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, true);
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Activated, alarm.setCondition(true));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, alarm.acknowledge());
    TEST_ASSERT_EQUAL(AlarmTransitionResult::ConditionRecovered,
                      alarm.setCondition(false));
    assertState(alarm, DomainAlarm::WaterLevel, false, true, true, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(false));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::LatchCleared, alarm.clearLatched());
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
}

void testCannotClearLatchedWhileConditionActive() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, true);
    alarm.setCondition(true);
    alarm.acknowledge();
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
    assertState(alarm, DomainAlarm::Temperature, true, true, true, true);
}

void testLatchedReoccurrenceResetsAck() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, true);
    alarm.setCondition(true);
    alarm.acknowledge();
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Reoccurred, alarm.setCondition(true));
    assertState(alarm, DomainAlarm::Temperature, true, true, false, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(true));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, alarm.acknowledge());
    assertState(alarm, DomainAlarm::Temperature, true, true, true, true);
}

void testLatchedCanAckAfterConditionRecovery() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, true);
    alarm.setCondition(true);
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, alarm.acknowledge());
    assertState(alarm, DomainAlarm::WaterLevel, false, true, true, true);
}

void testSnapshotIsIndependentValue() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, false);
    const AlarmSnapshot<DomainAlarm> before = alarm.snapshot();
    alarm.setCondition(true);
    TEST_ASSERT_FALSE(before.conditionActive);
    TEST_ASSERT_FALSE(before.alarmActive);
    assertState(alarm, DomainAlarm::WaterLevel, true, true, false, false);
}

void testMultipleAlarmOwnersAreIndependent() {
    AlarmState<DomainAlarm> first(DomainAlarm::WaterLevel, true);
    AlarmState<DomainAlarm> second(DomainAlarm::Temperature, false);
    AlarmState<OtherDomainAlarm> other(OtherDomainAlarm::Pump, false);
    first.setCondition(true);
    first.acknowledge();
    assertState(second, DomainAlarm::Temperature, false, false, false, false);
    TEST_ASSERT_FALSE(other.snapshot().alarmActive);
    second.setCondition(true);
    assertState(first, DomainAlarm::WaterLevel, true, true, true, true);
    assertState(second, DomainAlarm::Temperature, true, true, false, false);
}

void testPersistentValueTracksOnlyPendingLatch() {
    const AlarmPersistentState empty;
    TEST_ASSERT_FALSE(empty.latchedPending);
    AlarmState<DomainAlarm> latched(DomainAlarm::WaterLevel, true);
    AlarmState<DomainAlarm> nonLatched(DomainAlarm::Temperature, false);
    TEST_ASSERT_FALSE(latched.persistentState().latchedPending);
    TEST_ASSERT_FALSE(nonLatched.persistentState().latchedPending);
    latched.setCondition(true);
    nonLatched.setCondition(true);
    TEST_ASSERT_TRUE(latched.persistentState().latchedPending);
    TEST_ASSERT_FALSE(nonLatched.persistentState().latchedPending);
    latched.acknowledge();
    latched.setCondition(false);
    TEST_ASSERT_TRUE(latched.persistentState().latchedPending);
    latched.setCondition(true);
    TEST_ASSERT_FALSE(latched.snapshot().acknowledged);
    TEST_ASSERT_TRUE(latched.persistentState().latchedPending);
    latched.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::LatchCleared,
                      latched.clearLatched());
    TEST_ASSERT_FALSE(latched.persistentState().latchedPending);
}

void testRestorePendingNeedsLiveEvaluationBeforeClear() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored,
                      alarm.restore(AlarmPersistentState{true}));
    assertState(alarm, DomainAlarm::WaterLevel, false, true, false, true);
    TEST_ASSERT_TRUE(alarm.persistentState().latchedPending);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
    assertState(alarm, DomainAlarm::WaterLevel, false, true, false, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(false));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::LatchCleared, alarm.clearLatched());
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, true);
}

void testRestorePendingWithLiveActiveCondition() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored,
                      alarm.restore(AlarmPersistentState{true}));
    TEST_ASSERT_EQUAL(AlarmTransitionResult::ConditionConfirmed,
                      alarm.setCondition(true));
    assertState(alarm, DomainAlarm::WaterLevel, true, true, false, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.clearLatched());
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, alarm.setCondition(true));
    TEST_ASSERT_TRUE(alarm.persistentState().latchedPending);
}

void testRestoreAckIsRuntimeLocalAndRebootResetsIt() {
    AlarmState<DomainAlarm> first(DomainAlarm::Temperature, true);
    first.setCondition(true);
    first.acknowledge();
    first.setCondition(false);
    const AlarmPersistentState persisted = first.persistentState();
    AlarmState<DomainAlarm> second(DomainAlarm::Temperature, true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored, second.restore(persisted));
    assertState(second, DomainAlarm::Temperature, false, true, false, true);
    AlarmControl<DomainAlarm> control(second);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, control.acknowledge());
    assertState(second, DomainAlarm::Temperature, false, true, true, true);
    AlarmState<DomainAlarm> third(DomainAlarm::Temperature, true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored,
                      third.restore(second.persistentState()));
    assertState(third, DomainAlarm::Temperature, false, true, false, true);
}

void testIncompatibleRestoreFailsWithoutPartialState() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, false);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::IncompatibleState,
                      alarm.restore(AlarmPersistentState{true}));
    assertState(alarm, DomainAlarm::Temperature, false, false, false, false);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::AlreadyAttempted,
                      alarm.restore(AlarmPersistentState{false}));
    assertState(alarm, DomainAlarm::Temperature, false, false, false, false);
    alarm.setCondition(true);
    assertState(alarm, DomainAlarm::Temperature, true, true, false, false);
    TEST_ASSERT_FALSE(alarm.persistentState().latchedPending);
}

void testRestoreIsOneShotAndCannotOverwriteLiveState() {
    AlarmState<DomainAlarm> restored(DomainAlarm::WaterLevel, true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored,
                      restored.restore(AlarmPersistentState{true}));
    TEST_ASSERT_EQUAL(AlarmRestoreResult::AlreadyAttempted,
                      restored.restore(AlarmPersistentState{false}));
    TEST_ASSERT_TRUE(restored.persistentState().latchedPending);
    AlarmState<DomainAlarm> live(DomainAlarm::WaterLevel, true);
    live.setCondition(true);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::LiveStarted,
                      live.restore(AlarmPersistentState{false}));
    TEST_ASSERT_EQUAL(AlarmRestoreResult::AlreadyAttempted,
                      live.restore(AlarmPersistentState{true}));
    assertState(live, DomainAlarm::WaterLevel, true, true, false, true);
    AlarmState<DomainAlarm> evaluatedFalse(DomainAlarm::WaterLevel, true);
    evaluatedFalse.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::LiveStarted,
                      evaluatedFalse.restore(AlarmPersistentState{true}));
    assertState(evaluatedFalse, DomainAlarm::WaterLevel, false, false, false, true);
}

void testApplicationControlDoesNotOwnCondition() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::WaterLevel, true);
    AlarmControl<DomainAlarm> control(alarm);
    alarm.setCondition(true); // Domain-owned path.
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Acknowledged, control.acknowledge());
    assertState(alarm, DomainAlarm::WaterLevel, true, true, true, true);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, control.clearLatched());
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::LatchCleared, control.clearLatched());
    assertState(alarm, DomainAlarm::WaterLevel, false, false, false, true);
}

void testClearDoesNotRequireAck() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, true);
    alarm.setCondition(true);
    assertState(alarm, DomainAlarm::Temperature, true, true, false, true);
    alarm.setCondition(false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::LatchCleared, alarm.clearLatched());
    TEST_ASSERT_FALSE(alarm.persistentState().latchedPending);
}

void testRestoreAfterControlAttemptIsRejected() {
    AlarmState<DomainAlarm> alarm(DomainAlarm::Temperature, true);
    AlarmControl<DomainAlarm> control(alarm);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, control.acknowledge());
    TEST_ASSERT_EQUAL(AlarmRestoreResult::LiveStarted,
                      alarm.restore(AlarmPersistentState{true}));
    assertState(alarm, DomainAlarm::Temperature, false, false, false, true);
    AlarmState<DomainAlarm> clearAttempt(DomainAlarm::WaterLevel, true);
    AlarmControl<DomainAlarm> clearControl(clearAttempt);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::NoChange, clearControl.clearLatched());
    TEST_ASSERT_EQUAL(AlarmRestoreResult::LiveStarted,
                      clearAttempt.restore(AlarmPersistentState{true}));
    assertState(clearAttempt, DomainAlarm::WaterLevel, false, false, false, true);
}

void testNonLatchedRestartStartsInactiveAndReevaluatesCondition() {
    AlarmState<DomainAlarm> first(DomainAlarm::Temperature, false);
    first.setCondition(true);
    first.acknowledge();
    const AlarmPersistentState persisted = first.persistentState();
    TEST_ASSERT_FALSE(persisted.latchedPending);
    AlarmState<DomainAlarm> second(DomainAlarm::Temperature, false);
    TEST_ASSERT_EQUAL(AlarmRestoreResult::Restored, second.restore(persisted));
    assertState(second, DomainAlarm::Temperature, false, false, false, false);
    TEST_ASSERT_EQUAL(AlarmTransitionResult::Activated, second.setCondition(true));
    assertState(second, DomainAlarm::Temperature, true, true, false, false);
}

void runTests() {
    RUN_TEST(testInitialStateAndInactiveOperations);
    RUN_TEST(testNonLatchedActivationAckAndRecovery);
    RUN_TEST(testNonLatchedDuplicateSamplesDoNotResetAck);
    RUN_TEST(testLatchedRecoveryRequiresExplicitClear);
    RUN_TEST(testCannotClearLatchedWhileConditionActive);
    RUN_TEST(testLatchedReoccurrenceResetsAck);
    RUN_TEST(testLatchedCanAckAfterConditionRecovery);
    RUN_TEST(testSnapshotIsIndependentValue);
    RUN_TEST(testMultipleAlarmOwnersAreIndependent);
    RUN_TEST(testPersistentValueTracksOnlyPendingLatch);
    RUN_TEST(testRestorePendingNeedsLiveEvaluationBeforeClear);
    RUN_TEST(testRestorePendingWithLiveActiveCondition);
    RUN_TEST(testRestoreAckIsRuntimeLocalAndRebootResetsIt);
    RUN_TEST(testIncompatibleRestoreFailsWithoutPartialState);
    RUN_TEST(testRestoreIsOneShotAndCannotOverwriteLiveState);
    RUN_TEST(testApplicationControlDoesNotOwnCondition);
    RUN_TEST(testClearDoesNotRequireAck);
    RUN_TEST(testRestoreAfterControlAttemptIsRejected);
    RUN_TEST(testNonLatchedRestartStartsInactiveAndReevaluatesCondition);
    runAlarmIntegrationTests();
}

} // namespace

void setUp() {}
void tearDown() {}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    UNITY_BEGIN();
    runTests();
    UNITY_END();
}
void loop() {}
#else
int main() {
    UNITY_BEGIN();
    runTests();
    return UNITY_END();
}
#endif
