#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include <unity.h>

void runActionLockCoordinatorTests();

void setUp() {
}

void tearDown() {
}

void runTests() {
    runActionLockCoordinatorTests();
}

#if defined(ARDUINO)

void setup() {
    delay(2000);
    UNITY_BEGIN();
    runTests();
    UNITY_END();
}

void loop() {
}

#else

int main() {
    UNITY_BEGIN();
    runTests();
    return UNITY_END();
}

#endif
