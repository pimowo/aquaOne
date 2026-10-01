#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <stdint.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Events/EventSink.h"

namespace {

using AquaCore::Events::EventSink;
using AquaCore::Events::NullEventSink;

struct TemperatureChanged {
    int32_t milliCelsius;
    uint8_t sensor;
};

struct DosingStarted {
    uint8_t channel;
};

struct NonTrivialEvent {
    explicit NonTrivialEvent(int value) : value(value) {}
    int value;
};

static_assert(std::is_abstract<EventSink<TemperatureChanged>>::value,
    "EventSink must be an interface");
static_assert(!std::has_virtual_destructor<EventSink<TemperatureChanged>>::value,
    "Borrowed EventSink must not require virtual destruction");
static_assert(!std::is_convertible<EventSink<TemperatureChanged>*,
    EventSink<DosingStarted>*>::value, "Event types must remain separate");

class TemperatureSink final : public EventSink<TemperatureChanged> {
public:
    void emit(const TemperatureChanged& event) override {
        ++calls;
        lastTemperature = event.milliCelsius;
        lastSensor = event.sensor;
    }

    unsigned calls = 0U;
    int32_t lastTemperature = 0;
    uint8_t lastSensor = 0U;
};

class DosingSink final : public EventSink<DosingStarted> {
public:
    void emit(const DosingStarted& event) override {
        ++calls;
        lastChannel = event.channel;
    }

    unsigned calls = 0U;
    uint8_t lastChannel = 0U;
};

class NonTrivialSink final : public EventSink<NonTrivialEvent> {
public:
    void emit(const NonTrivialEvent& event) override { observed = event.value; }
    int observed = 0;
};

void emitTemperature(EventSink<TemperatureChanged>& sink,
                     const TemperatureChanged& event) {
    sink.emit(event);
}

void testTypedEventAndFields() {
    TemperatureSink adapter;
    EventSink<TemperatureChanged>& borrowed = adapter;
    const TemperatureChanged event = {23500, 3U};
    emitTemperature(borrowed, event);
    TEST_ASSERT_EQUAL_UINT(1U, adapter.calls);
    TEST_ASSERT_EQUAL_INT32(23500, adapter.lastTemperature);
    TEST_ASSERT_EQUAL_UINT8(3U, adapter.lastSensor);
}

void testInputRemainsUnchanged() {
    TemperatureSink adapter;
    TemperatureChanged event = {19250, 7U};
    adapter.emit(event);
    TEST_ASSERT_EQUAL_INT32(19250, event.milliCelsius);
    TEST_ASSERT_EQUAL_UINT8(7U, event.sensor);
}

void testIndependentSinks() {
    TemperatureSink first;
    TemperatureSink second;
    first.emit(TemperatureChanged{1000, 1U});
    second.emit(TemperatureChanged{2000, 2U});
    first.emit(TemperatureChanged{3000, 3U});
    TEST_ASSERT_EQUAL_UINT(2U, first.calls);
    TEST_ASSERT_EQUAL_INT32(3000, first.lastTemperature);
    TEST_ASSERT_EQUAL_UINT(1U, second.calls);
    TEST_ASSERT_EQUAL_INT32(2000, second.lastTemperature);
}

void testDistinctEventTypesAndNonTrivialPayload() {
    TemperatureSink temperature;
    DosingSink dosing;
    NonTrivialSink nonTrivial;
    temperature.emit(TemperatureChanged{21000, 4U});
    dosing.emit(DosingStarted{6U});
    nonTrivial.emit(NonTrivialEvent(42));
    TEST_ASSERT_EQUAL_UINT8(4U, temperature.lastSensor);
    TEST_ASSERT_EQUAL_UINT8(6U, dosing.lastChannel);
    TEST_ASSERT_EQUAL_INT(42, nonTrivial.observed);
}

void testBorrowedStackSinkAndCallerOwnedTemporary() {
    TemperatureSink adapter;
    EventSink<TemperatureChanged>& borrowed = adapter;
    {
        const TemperatureChanged event = {26000, 5U};
        borrowed.emit(event);
    }
    // The adapter kept only copied fields; the interface retains no input.
    TEST_ASSERT_EQUAL_INT32(26000, adapter.lastTemperature);
    TEST_ASSERT_EQUAL_UINT8(5U, adapter.lastSensor);
}

void testExplicitNullSink() {
    NullEventSink<TemperatureChanged> discard;
    EventSink<TemperatureChanged>& borrowed = discard;
    TemperatureChanged event = {18000, 1U};
    borrowed.emit(event);
    TEST_ASSERT_EQUAL_INT32(18000, event.milliCelsius);
    TEST_ASSERT_EQUAL_UINT8(1U, event.sensor);
}

void runTests() {
    RUN_TEST(testTypedEventAndFields);
    RUN_TEST(testInputRemainsUnchanged);
    RUN_TEST(testIndependentSinks);
    RUN_TEST(testDistinctEventTypesAndNonTrivialPayload);
    RUN_TEST(testBorrowedStackSinkAndCallerOwnedTemporary);
    RUN_TEST(testExplicitNullSink);
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
