#include <stdint.h>
#include <unity.h>

#include "AquaCore/Maintenance/NormalProcessing.h"

namespace {

using AquaCore::Maintenance::NormalProcessingDecision;
using AquaCore::Maintenance::evaluateNormalProcessing;
using AquaCore::System::OperationalState;

void testNormalProcessingStateMatrix() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NormalProcessingDecision::Allowed),
        static_cast<int>(evaluateNormalProcessing(OperationalState::RUNNING)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NormalProcessingDecision::Blocked),
        static_cast<int>(evaluateNormalProcessing(OperationalState::MAINTENANCE)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NormalProcessingDecision::Blocked),
        static_cast<int>(evaluateNormalProcessing(OperationalState::BOOTING)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NormalProcessingDecision::Blocked),
        static_cast<int>(evaluateNormalProcessing(OperationalState::ERROR)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NormalProcessingDecision::Blocked),
        static_cast<int>(evaluateNormalProcessing(
            static_cast<OperationalState>(0xFFU))));
}

void testNormalProcessingDecisionIsRepeatable() {
    const OperationalState states[] = {
        OperationalState::MAINTENANCE,
        OperationalState::RUNNING
    };
    const NormalProcessingDecision expected[] = {
        NormalProcessingDecision::Blocked,
        NormalProcessingDecision::Allowed
    };
    for (unsigned index = 0U; index < 2U; ++index) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[index]),
            static_cast<int>(evaluateNormalProcessing(states[index])));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[index]),
            static_cast<int>(evaluateNormalProcessing(states[index])));
    }
}

} // namespace

void runNormalProcessingTests() {
    RUN_TEST(testNormalProcessingStateMatrix);
    RUN_TEST(testNormalProcessingDecisionIsRepeatable);
}
