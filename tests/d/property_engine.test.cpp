#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "prototypes/d/engine.h"
#include "prototypes/d/property.h"
#include "prototypes/d/store.h"
#include "prototypes/d/strategies.h"
#include "prototypes/d/value.h"

namespace albor::prototype_d
{

    namespace
    {

        // stand in for the generated binding shape of a plain function plus declared dependencies (§13.3)
        void copy_binding(BindingContext& context) { context.write(context.read(context.declared().front())); }

        // add one to the dependency so that two of them never settle in a cycle
        void increment_binding(BindingContext& context) { context.write_double(context.read_double(context.declared().front()) + 1.0); }

        void sum_binding(BindingContext& context) {
            double total = 0.0;

            for (const PropertyId dependency : context.declared()) {
                total += context.read_double(dependency);
            }

            context.write_double(total);
        }

    } // namespace

    TEST(PropertyEngineChecks, stores_and_reads_values_by_id) {
        // arrange
        PropertyStore store;
        const PropertyId first = store.add(Value(std::int64_t{7}));
        const PropertyId second = store.add(Value(2.5));

        // act
        const std::int64_t read_first = store.read(first, Value(std::int64_t{0})).as_int();
        const double read_second = store.read(second, Value(0.0)).as_double();

        // assert
        ASSERT_EQ(read_first, 7);
        ASSERT_DOUBLE_EQ(read_second, 2.5);
    }

    TEST(PropertyEngineChecks, an_unchanged_assignment_does_not_advance_the_generation) {
        // arrange
        PropertyStore store;
        const PropertyId id = store.add(Value(std::int64_t{1}));
        const std::uint64_t generation_before = store.generation_of(id);

        // act
        const bool changed = store.assign(id, Value(std::int64_t{1}));

        // assert
        ASSERT_FALSE(changed);
        ASSERT_EQ(store.generation_of(id), generation_before);
    }

    TEST(PropertyEngineChecks, a_changed_assignment_advances_the_generation) {
        // arrange
        PropertyStore store;
        const PropertyId id = store.add(Value(std::int64_t{1}));
        const std::uint64_t generation_before = store.generation_of(id);

        // act
        const bool changed = store.assign(id, Value(std::int64_t{2}));

        // assert
        ASSERT_TRUE(changed);
        ASSERT_EQ(store.generation_of(id), generation_before + 1);
    }

    TEST(PropertyEngineChecks, a_binding_runs_once_for_a_changed_dependency) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();

        // act
        engine.assign(source, Value(4.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_DOUBLE_EQ(store.read(derived, Value(0.0)).as_double(), 4.0);
        ASSERT_EQ(stats.bindings_evaluated, 1u);
    }

    TEST(PropertyEngineChecks, an_unchanged_dependency_does_not_run_the_binding) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();
        engine.assign(source, Value(4.0));
        engine.evaluate();

        // act
        engine.assign(source, Value(4.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_EQ(stats.bindings_evaluated, 0u);
    }

    TEST(PropertyEngineChecks, a_chain_settles_in_dependency_order) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<DirtyBitsetStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId middle = store.add(Value(0.0));
        const PropertyId tip = store.add(Value(0.0));
        const std::vector<PropertyId> first_dependencies{source};
        const std::vector<PropertyId> second_dependencies{middle};
        engine.add_binding(middle, first_dependencies, &copy_binding);
        engine.add_binding(tip, second_dependencies, &copy_binding);
        engine.seal();

        // act
        engine.assign(source, Value(9.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_DOUBLE_EQ(store.read(middle, Value(0.0)).as_double(), 9.0);
        ASSERT_DOUBLE_EQ(store.read(tip, Value(0.0)).as_double(), 9.0);
        ASSERT_LE(stats.passes, 2u);
        ASSERT_FALSE(stats.cycle_budget_exhausted);
    }

    TEST(PropertyEngineChecks, a_binding_write_does_not_break_its_own_binding) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();

        // act
        engine.assign(source, Value(3.0));
        engine.evaluate();
        const WriteSource source_after_evaluation = store.source_of(derived);

        // assert
        ASSERT_EQ(source_after_evaluation, WriteSource::Binding);
        ASSERT_TRUE(store.has_binding(derived));
    }

    TEST(PropertyEngineChecks, an_imperative_assignment_breaks_the_binding) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();

        // act
        engine.assign(derived, Value(42.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_FALSE(store.has_binding(derived));
        ASSERT_EQ(store.source_of(derived), WriteSource::Imperative);
        ASSERT_DOUBLE_EQ(store.read(derived, Value(0.0)).as_double(), 42.0);
        ASSERT_EQ(stats.bindings_evaluated, 0u);
    }

    TEST(PropertyEngineChecks, rebind_restores_the_declarative_binding) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        const BindingId binding = engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();
        engine.assign(derived, Value(42.0));
        engine.evaluate();

        // act
        engine.rebind(binding);
        engine.evaluate();

        // assert
        ASSERT_TRUE(store.has_binding(derived));
        ASSERT_DOUBLE_EQ(store.read(derived, Value(0.0)).as_double(), 42.0);
    }

    TEST(PropertyEngineChecks, a_worker_publishes_through_the_thread_safe_inbox) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(std::int64_t{0}));
        const PropertyId derived = store.add(Value(std::int64_t{0}));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();

        // act
        std::thread worker([&store, source] { store.post_set(source, Value(std::int64_t{11})); });
        worker.join();
        ASSERT_EQ(store.pending_write_count(), 1u);
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_EQ(store.pending_write_count(), 0u);
        ASSERT_EQ(store.read(derived, Value(std::int64_t{0})).as_int(), 11);
        ASSERT_EQ(stats.bindings_evaluated, 1u);
    }

    TEST(PropertyEngineChecks, a_worker_write_of_an_equal_value_does_not_invalidate) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(std::int64_t{5}));
        const PropertyId derived = store.add(Value(std::int64_t{0}));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();
        engine.post_set(source, Value(std::int64_t{5}));

        // act
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_EQ(stats.bindings_evaluated, 0u);
    }

    TEST(PropertyEngineChecks, a_binding_summing_its_dependencies_reads_them_all) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId left = store.add(Value(2.0));
        const PropertyId right = store.add(Value(3.0));
        const PropertyId total = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{left, right};
        engine.add_binding(total, dependencies, &sum_binding);
        engine.seal();

        // act
        engine.assign(right, Value(10.0));
        engine.evaluate();

        // assert
        ASSERT_DOUBLE_EQ(store.read(total, Value(0.0)).as_double(), 12.0);
    }

    TEST(PropertyEngineChecks, a_cycle_that_never_settles_stops_at_the_pass_budget) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<DirtyBitsetStrategy>());
        const PropertyId seed = store.add(Value(1.0));
        const PropertyId left = store.add(Value(1.0));
        const PropertyId right = store.add(Value(1.0));
        const std::vector<PropertyId> seed_dependencies{seed};
        const std::vector<PropertyId> left_dependencies{left};
        const std::vector<PropertyId> right_dependencies{right};
        engine.add_binding(left, seed_dependencies, &copy_binding);
        engine.add_binding(right, left_dependencies, &increment_binding);
        engine.add_binding(left, right_dependencies, &increment_binding);
        engine.seal();

        // act
        engine.assign(seed, Value(5.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_TRUE(stats.cycle_budget_exhausted);
        ASSERT_GT(stats.passes, 1u);
        ASSERT_EQ(stats.bindings_skipped, 0u);
    }

    TEST(PropertyEngineChecks, an_animation_write_shadows_the_binding_value) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId animated = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(animated, dependencies, &copy_binding);
        engine.seal();
        engine.assign(source, Value(4.0));
        engine.evaluate();

        // act
        const bool changed = engine.set_animation(animated, Value(99.0));

        // assert
        ASSERT_TRUE(changed);
        ASSERT_DOUBLE_EQ(store.read(animated, Value(0.0)).as_double(), 99.0);
        ASSERT_EQ(store.source_of(animated), WriteSource::Animation);
        ASSERT_EQ(store.animated_count(), 1u);
    }

    TEST(PropertyEngineChecks, a_binding_does_not_overwrite_an_animated_property) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId animated = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(animated, dependencies, &copy_binding);
        engine.seal();
        static_cast<void>(engine.set_animation(animated, Value(99.0)));

        // act
        engine.assign(source, Value(4.0));
        engine.evaluate();

        // assert
        ASSERT_DOUBLE_EQ(store.read(animated, Value(0.0)).as_double(), 99.0);
    }

    TEST(PropertyEngineChecks, ending_an_animation_restores_the_shadowed_binding_value) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId animated = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(animated, dependencies, &copy_binding);
        engine.seal();
        engine.assign(source, Value(4.0));
        engine.evaluate();
        static_cast<void>(engine.set_animation(animated, Value(99.0)));

        // act
        engine.end_animation(animated);
        engine.evaluate();

        // assert
        ASSERT_DOUBLE_EQ(store.read(animated, Value(0.0)).as_double(), 4.0);
        ASSERT_FALSE(store.animated(animated));
        ASSERT_EQ(store.animated_count(), 0u);
    }

    TEST(PropertyEngineChecks, an_imperative_assignment_wins_over_an_active_animation) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId animated = store.add(Value(0.0));
        static_cast<void>(engine.set_animation(animated, Value(1.0)));

        // act
        engine.assign(animated, Value(7.0));

        // assert
        ASSERT_DOUBLE_EQ(store.read(animated, Value(0.0)).as_double(), 7.0);
        ASSERT_EQ(store.source_of(animated), WriteSource::Imperative);
        ASSERT_EQ(store.animated_count(), 0u);
    }

    TEST(PropertyEngineChecks, an_animation_write_is_skipped_when_the_value_is_equal) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId animated = store.add(Value(0.0));

        // act
        const bool first = engine.set_animation(animated, Value(5.0));
        const bool second = engine.set_animation(animated, Value(5.0));

        // assert
        ASSERT_TRUE(first);
        ASSERT_FALSE(second);
        ASSERT_EQ(store.animated_count(), 1u);
    }

    TEST(PropertyEngineChecks, the_engine_counts_the_properties_it_invalidated) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();
        engine.reset_invalidated_total();

        // act
        engine.assign(source, Value(3.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_EQ(stats.properties_invalidated, 1u);
        ASSERT_EQ(engine.invalidated_total(), 2u);
    }

    TEST(PropertyEngineChecks, a_planned_binding_is_counted_once_per_update) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));
        const PropertyId derived = store.add(Value(0.0));
        const std::vector<PropertyId> dependencies{source};
        engine.add_binding(derived, dependencies, &copy_binding);
        engine.seal();

        // act
        engine.assign(source, Value(3.0));
        const EvaluationStats stats = engine.evaluate();

        // assert
        ASSERT_EQ(stats.bindings_planned, 1u);
        ASSERT_EQ(stats.candidates_considered, 1u);
    }

    TEST(PropertyEngineChecks, a_typed_property_reads_and_writes_its_slot) {
        // arrange
        PropertyStore store;
        Property<double> speed(store, store.add(Value(1.0)));

        // act
        speed.set(2.5);
        const double read_back = speed.get();

        // assert
        ASSERT_DOUBLE_EQ(read_back, 2.5);
    }

    TEST(PropertyEngineChecks, a_snapshot_value_shares_immutable_storage) {
        // arrange
        PropertyStore store;
        const std::shared_ptr<const std::vector<double>> samples = std::make_shared<const std::vector<double>>(4, 1.0);
        const SnapshotBox box(ValueType::Double, std::shared_ptr<const void>(samples), 4);
        const PropertyId id = store.add(Value(box));

        // act
        const SnapshotBox read_back = store.read(id, Value(SnapshotBox())).as_snapshot();

        // assert
        ASSERT_EQ(read_back.count(), 4u);
        ASSERT_EQ(read_back.element_type(), ValueType::Double);
        ASSERT_EQ(read_back.data(), static_cast<const void*>(samples.get()));
    }

    TEST(PropertyEngineChecks, the_binding_table_costs_no_indirect_call_object) {
        // arrange
        PropertyStore store;
        BindingEngine engine(store, std::make_unique<CompactRecordsStrategy>());
        const PropertyId source = store.add(Value(1.0));

        // act
        for (std::size_t index = 0; index < 1000; index++) {
            const PropertyId derived = store.add(Value(0.0));
            const std::vector<PropertyId> dependencies{source};
            engine.add_binding(derived, dependencies, &copy_binding);
        }

        engine.seal();
        const EngineMemory usage = engine.memory();

        // assert
        ASSERT_EQ(engine.binding_count(), 1000u);
        ASSERT_EQ(usage.binding_records, usage.binding_records);
        ASSERT_LT(usage.binding_records / 1000, 64u);
        ASSERT_EQ(usage.declared_dependencies / 1000, sizeof(PropertyId));
    }

} // namespace albor::prototype_d
