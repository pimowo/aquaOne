#include <type_traits>
#include <unity.h>

#include "AquaCore/Maintenance/MaintenanceParticipant.h"

namespace {

using AquaCore::Maintenance::MaintenanceParticipant;
using AquaCore::Maintenance::MaintenanceParticipantResult;

static_assert(!std::has_virtual_destructor<MaintenanceParticipant>::value,
              "A borrowed MaintenanceParticipant has no virtual destructor");
static_assert(!std::is_destructible<MaintenanceParticipant>::value,
              "MaintenanceParticipant cannot be destroyed through its base interface");

class FakeMaintenanceParticipant final : public MaintenanceParticipant {
public:
    MaintenanceParticipantResult enterResult = MaintenanceParticipantResult::Prepared;
    MaintenanceParticipantResult exitResult = MaintenanceParticipantResult::Prepared;
    unsigned enterCalls = 0U;
    unsigned exitCalls = 0U;

    MaintenanceParticipantResult prepareEnter() override {
        ++enterCalls;
        return enterResult;
    }

    MaintenanceParticipantResult prepareExit() override {
        ++exitCalls;
        return exitResult;
    }
};

void testBorrowedPrepareEnterReturnsAllResults() {
    FakeMaintenanceParticipant concrete;
    MaintenanceParticipant& participant = concrete;

    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Prepared,
                      participant.prepareEnter());
    concrete.enterResult = MaintenanceParticipantResult::Rejected;
    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Rejected,
                      participant.prepareEnter());
    concrete.enterResult = MaintenanceParticipantResult::Failed;
    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Failed,
                      participant.prepareEnter());
    TEST_ASSERT_EQUAL_UINT(3U, concrete.enterCalls);
    TEST_ASSERT_EQUAL_UINT(0U, concrete.exitCalls);
}

void testBorrowedPrepareExitReturnsAllResults() {
    FakeMaintenanceParticipant concrete;
    MaintenanceParticipant& participant = concrete;

    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Prepared,
                      participant.prepareExit());
    concrete.exitResult = MaintenanceParticipantResult::Rejected;
    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Rejected,
                      participant.prepareExit());
    concrete.exitResult = MaintenanceParticipantResult::Failed;
    TEST_ASSERT_EQUAL(MaintenanceParticipantResult::Failed,
                      participant.prepareExit());
    TEST_ASSERT_EQUAL_UINT(0U, concrete.enterCalls);
    TEST_ASSERT_EQUAL_UINT(3U, concrete.exitCalls);
}

} // namespace

void runMaintenanceParticipantTests() {
    RUN_TEST(testBorrowedPrepareEnterReturnsAllResults);
    RUN_TEST(testBorrowedPrepareExitReturnsAllResults);
}
