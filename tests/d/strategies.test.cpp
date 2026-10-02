#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <vector>

#include "prototypes/d/strategies.h"

namespace albor::prototype_d
{

    namespace
    {

        // drive invalidation directly to inspect the plan a strategy produces
        std::vector<BindingId> plan_for(InvalidationStrategy& strategy, std::vector<PropertyId> changed) {
            std::vector<BindingId> plan;

            for (const PropertyId property : changed) {
                strategy.invalidate(property);
            }

            strategy.plan(plan);
            return plan;
        }

    } // namespace

    TEST(InvalidationStrategyChecks, compact_records_collects_the_bindings_of_a_changed_property) {
        // arrange
        CompactRecordsStrategy strategy;
        const std::vector<PropertyId> first_dependencies{0, 1};
        const std::vector<PropertyId> second_dependencies{1};
        strategy.declare(0, first_dependencies, 4);
        strategy.declare(1, second_dependencies, 5);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {1});

        // assert
        ASSERT_EQ(plan.size(), 2u);
        ASSERT_TRUE(std::ranges::find(plan, 0) != plan.end());
        ASSERT_TRUE(std::ranges::find(plan, 1) != plan.end());
    }

    TEST(InvalidationStrategyChecks, compact_records_deduplicates_a_binding_reached_twice) {
        // arrange
        CompactRecordsStrategy strategy;
        const std::vector<PropertyId> dependencies{0, 1};
        strategy.declare(0, dependencies, 4);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {0, 1});

        // assert
        ASSERT_EQ(plan.size(), 1u);
        ASSERT_EQ(strategy.last_candidate_count(), 2u);
    }

    TEST(InvalidationStrategyChecks, dirty_bitset_orders_a_producer_before_its_consumer) {
        // arrange
        DirtyBitsetStrategy strategy;
        const std::vector<PropertyId> consumer_dependencies{2};
        const std::vector<PropertyId> producer_dependencies{0};
        strategy.declare(0, consumer_dependencies, 3);
        strategy.declare(1, producer_dependencies, 2);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {0, 2});

        // assert
        ASSERT_EQ(plan.size(), 2u);
        ASSERT_EQ(plan.at(0), 1u);
        ASSERT_EQ(plan.at(1), 0u);
        ASSERT_EQ(strategy.last_cycle_count(), 0u);
    }

    TEST(InvalidationStrategyChecks, dirty_bitset_reports_a_dependency_cycle) {
        // arrange
        DirtyBitsetStrategy strategy;
        const std::vector<PropertyId> left_dependencies{2};
        const std::vector<PropertyId> right_dependencies{1};
        strategy.declare(0, left_dependencies, 1);
        strategy.declare(1, right_dependencies, 2);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {1, 2});

        // assert
        ASSERT_EQ(plan.size(), 2u);
        ASSERT_EQ(strategy.last_cycle_count(), 2u);
    }

    TEST(InvalidationStrategyChecks, dirty_bitset_clears_its_dirty_state_after_a_pass) {
        // arrange
        DirtyBitsetStrategy strategy;
        const std::vector<PropertyId> dependencies{0};
        strategy.declare(0, dependencies, 1);
        strategy.finish(8);
        static_cast<void>(plan_for(strategy, {0}));

        // act
        std::vector<BindingId> plan;
        strategy.plan(plan);

        // assert
        ASSERT_TRUE(plan.empty());
    }

    TEST(InvalidationStrategyChecks, shared_nodes_collapse_identical_dependency_sets) {
        // arrange
        SharedNodesStrategy strategy;
        const std::vector<PropertyId> first_dependencies{0, 1};
        const std::vector<PropertyId> second_dependencies{1, 0};
        const std::vector<PropertyId> third_dependencies{0, 2};
        strategy.declare(0, first_dependencies, 4);
        strategy.declare(1, second_dependencies, 5);
        strategy.declare(2, third_dependencies, 6);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {0});

        // assert
        ASSERT_EQ(strategy.node_count(), 2u);
        ASSERT_EQ(plan.size(), 3u);
        ASSERT_TRUE(std::ranges::find(plan, 0) != plan.end());
        ASSERT_TRUE(std::ranges::find(plan, 1) != plan.end());
        ASSERT_TRUE(std::ranges::find(plan, 2) != plan.end());
    }

    TEST(InvalidationStrategyChecks, shared_nodes_remember_fewer_dependencies_than_they_declare) {
        // arrange
        SharedNodesStrategy strategy;
        const std::vector<PropertyId> first_dependencies{0, 1};
        const std::vector<PropertyId> second_dependencies{0, 1};
        strategy.declare(0, first_dependencies, 4);
        strategy.declare(1, second_dependencies, 5);
        strategy.finish(8);

        // act
        const std::size_t remembered = strategy.remembered_dependency_count();

        // assert
        ASSERT_EQ(remembered, 4u);
    }

    TEST(InvalidationStrategyChecks, every_strategy_reports_its_own_name) {
        // arrange
        CompactRecordsStrategy compact;
        DirtyBitsetStrategy dirty;
        SharedNodesStrategy shared;

        // act / assert
        ASSERT_EQ(compact.name(), "compact_records");
        ASSERT_EQ(dirty.name(), "dirty_bitset_topo");
        ASSERT_EQ(shared.name(), "shared_dep_nodes");
    }

    TEST(InvalidationStrategyChecks, a_plan_is_empty_without_an_invalidation) {
        // arrange
        CompactRecordsStrategy strategy;
        const std::vector<PropertyId> dependencies{0};
        strategy.declare(0, dependencies, 1);
        strategy.finish(8);

        // act
        std::vector<BindingId> plan;
        strategy.plan(plan);

        // assert
        ASSERT_TRUE(plan.empty());
    }

    TEST(InvalidationStrategyChecks, a_binding_without_dependencies_is_never_planned) {
        // arrange
        CompactRecordsStrategy strategy;
        strategy.declare(0, {}, 1);
        strategy.finish(8);

        // act
        const std::vector<BindingId> plan = plan_for(strategy, {0});

        // assert
        ASSERT_TRUE(plan.empty());
    }

} // namespace albor::prototype_d
