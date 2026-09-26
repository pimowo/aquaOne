#include <unity.h>

#include <type_traits>

#include <AquaCore/Safety/ActionLockCoordinator.h>

using AquaCore::Safety::ActionLockContribution;
using AquaCore::Safety::ActionLockCoordinator;
using AquaCore::Safety::ActionLockProvider;
using AquaCore::Safety::ActionLockState;

namespace {

enum class ExampleAction { Fill, Drain };

static_assert(
    !std::has_virtual_destructor<ActionLockProvider<ExampleAction>>::value,
    "Action lock providers are borrowed and not destroyed through the base interface"
);

class ExampleLockProvider final : public ActionLockProvider<ExampleAction> {
public:
    ActionLockContribution queryActionLock(const ExampleAction& action) const override {
        if (action == ExampleAction::Fill) {
            return ActionLockContribution(fillLocked, fillSafetyCritical);
        }
        return ActionLockContribution(drainLocked, drainSafetyCritical);
    }

    bool fillLocked = false;
    bool fillSafetyCritical = false;
    bool drainLocked = false;
    bool drainSafetyCritical = false;
};

void test_zero_providers_allows_action() {
    ActionLockCoordinator<ExampleAction> coordinator(nullptr, 0U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(coordinator.isCompositionValid());
    TEST_ASSERT_TRUE(state.compositionValid);
    TEST_ASSERT_FALSE(state.locked);
    TEST_ASSERT_FALSE(state.hasSafetyCriticalLock);
    TEST_ASSERT_EQUAL_UINT(0U, state.activeProviderCount);
}

void test_one_clear_provider_allows_action() {
    ExampleLockProvider provider;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.compositionValid);
    TEST_ASSERT_FALSE(state.locked);
    TEST_ASSERT_EQUAL_UINT(0U, state.activeProviderCount);
}

void test_one_provider_can_lock_action() {
    ExampleLockProvider provider;
    provider.fillLocked = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_FALSE(state.hasSafetyCriticalLock);
    TEST_ASSERT_EQUAL_UINT(1U, state.activeProviderCount);
}

void test_lock_from_either_provider_is_aggregated() {
    ExampleLockProvider first;
    ExampleLockProvider second;
    second.fillLocked = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &first, &second };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 2U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_EQUAL_UINT(1U, state.activeProviderCount);
}

void test_independent_locks_clear_only_when_all_are_gone() {
    ExampleLockProvider first;
    ExampleLockProvider second;
    first.fillLocked = true;
    second.fillLocked = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &first, &second };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 2U);
    TEST_ASSERT_TRUE(coordinator.query(ExampleAction::Fill).locked);
    first.fillLocked = false;
    ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_EQUAL_UINT(1U, state.activeProviderCount);
    second.fillLocked = false;
    state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_FALSE(state.locked);
    TEST_ASSERT_EQUAL_UINT(0U, state.activeProviderCount);
}

void test_safety_critical_lock_is_reported() {
    ExampleLockProvider provider;
    provider.fillLocked = true;
    provider.fillSafetyCritical = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    TEST_ASSERT_TRUE(coordinator.query(ExampleAction::Fill).hasSafetyCriticalLock);
}

void test_non_safety_lock_does_not_report_safety_critical() {
    ExampleLockProvider provider;
    provider.fillLocked = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_FALSE(state.hasSafetyCriticalLock);
}

void test_mixed_locks_report_safety_critical_if_any_is_critical() {
    ExampleLockProvider nonCritical;
    ExampleLockProvider critical;
    nonCritical.fillLocked = true;
    critical.fillLocked = true;
    critical.fillSafetyCritical = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &nonCritical, &critical };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 2U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_TRUE(state.hasSafetyCriticalLock);
    TEST_ASSERT_EQUAL_UINT(2U, state.activeProviderCount);
}

void test_null_provider_fails_closed() {
    ExampleLockProvider provider;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider, nullptr };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 2U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_FALSE(coordinator.isCompositionValid());
    TEST_ASSERT_FALSE(state.compositionValid);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_TRUE(state.hasSafetyCriticalLock);
    TEST_ASSERT_EQUAL_UINT(0U, state.activeProviderCount);
}

void test_mismatched_empty_provider_view_fails_closed() {
    ActionLockCoordinator<ExampleAction> coordinator(nullptr, 1U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_FALSE(state.compositionValid);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_TRUE(state.hasSafetyCriticalLock);
}

void test_coordinator_instances_are_independent() {
    ExampleLockProvider lockedProvider;
    ExampleLockProvider clearProvider;
    lockedProvider.fillLocked = true;
    const ActionLockProvider<ExampleAction>* lockedList[] = { &lockedProvider };
    const ActionLockProvider<ExampleAction>* clearList[] = { &clearProvider };
    ActionLockCoordinator<ExampleAction> locked(lockedList, 1U);
    ActionLockCoordinator<ExampleAction> clear(clearList, 1U);
    TEST_ASSERT_TRUE(locked.query(ExampleAction::Fill).locked);
    TEST_ASSERT_FALSE(clear.query(ExampleAction::Fill).locked);
}

void test_actions_are_queried_independently() {
    ExampleLockProvider provider;
    provider.fillLocked = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    TEST_ASSERT_TRUE(coordinator.query(ExampleAction::Fill).locked);
    TEST_ASSERT_FALSE(coordinator.query(ExampleAction::Drain).locked);
}

void test_provider_changes_are_observed_on_next_query() {
    ExampleLockProvider provider;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    TEST_ASSERT_FALSE(coordinator.query(ExampleAction::Fill).locked);
    provider.fillLocked = true;
    TEST_ASSERT_TRUE(coordinator.query(ExampleAction::Fill).locked);
}

void test_invalid_contribution_fails_closed() {
    ExampleLockProvider provider;
    provider.fillSafetyCritical = true;
    const ActionLockProvider<ExampleAction>* providers[] = { &provider };
    ActionLockCoordinator<ExampleAction> coordinator(providers, 1U);
    const ActionLockState state = coordinator.query(ExampleAction::Fill);
    TEST_ASSERT_FALSE(state.compositionValid);
    TEST_ASSERT_TRUE(state.locked);
    TEST_ASSERT_TRUE(state.hasSafetyCriticalLock);
}

} // namespace

void runActionLockCoordinatorTests() {
    RUN_TEST(test_zero_providers_allows_action);
    RUN_TEST(test_one_clear_provider_allows_action);
    RUN_TEST(test_one_provider_can_lock_action);
    RUN_TEST(test_lock_from_either_provider_is_aggregated);
    RUN_TEST(test_independent_locks_clear_only_when_all_are_gone);
    RUN_TEST(test_safety_critical_lock_is_reported);
    RUN_TEST(test_non_safety_lock_does_not_report_safety_critical);
    RUN_TEST(test_mixed_locks_report_safety_critical_if_any_is_critical);
    RUN_TEST(test_null_provider_fails_closed);
    RUN_TEST(test_mismatched_empty_provider_view_fails_closed);
    RUN_TEST(test_coordinator_instances_are_independent);
    RUN_TEST(test_actions_are_queried_independently);
    RUN_TEST(test_provider_changes_are_observed_on_next_query);
    RUN_TEST(test_invalid_contribution_fails_closed);
}
