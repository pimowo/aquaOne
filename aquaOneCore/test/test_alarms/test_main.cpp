#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <type_traits>
#include <unity.h>

#include "AquaCore/Alarms/AlarmState.h"

namespace {

using AquaCore::Alarms::AlarmSnapshot;
using AquaCore::Alarms::AlarmState;
using AquaCore::Alarms::AlarmTransitionResult;

enum class DomainAlarm { WaterLevel, Temperature };
enum class OtherDomainAlarm { Pump };

static_assert(!std::is_copy_constructible<AlarmState<DomainAlarm>>::value,
              "Alarm owner must not be copied");
static_assert(!std::is_convertible<AlarmState<DomainAlarm>*,
                                   AlarmState<OtherDomainAlarm>*>::value,
              "Domain alarm ID types must remain separate");

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
