#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "prototypes/d2/src/cycles.h"
#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/index.h"
#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

namespace albor::prototype_d2
{

    namespace
    {

        // build a sealed scene over the erased store, which is what most cases need (§13.3)
        std::unique_ptr<Scene<ErasedStore>> make_scene(EvalModel model, bool mutable_index) { return std::make_unique<Scene<ErasedStore>>(model, mutable_index); }

    } // namespace

    // ===================================================================================================================
    // typed property values
    // ===================================================================================================================

    TEST(D2TypedValues, a_typed_store_round_trips_every_value_category) {
        // arrange
        RegionTable regions;
        TypedStore store(regions);
        const SchemaId schema = regions.add_schema("Mixed", InstanceKind::Component, realistic_types());
        const InstanceId instance = store.create(schema);

        // act
        store.write(regions.global(instance, 0), Value(std::int64_t{7}), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 1), Value(2.5), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 2), Value(true), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 3), Value::make_string("hello"), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 4), Value::make_translated("bonjour", 3), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 5), Value::make_snapshot(std::make_shared<const std::vector<double>>(4, 1.5)), WriteSource::GeneratedInit);

        // assert
        ASSERT_EQ(store.read(regions.global(instance, 0)).as_int(), 7);
        ASSERT_DOUBLE_EQ(store.read(regions.global(instance, 1)).as_double(), 2.5);
        ASSERT_TRUE(store.read(regions.global(instance, 2)).as_bool());
        ASSERT_EQ(*store.read(regions.global(instance, 3)).as_string(), "hello");
        ASSERT_EQ(store.read(regions.global(instance, 4)).as_translated().locale_id(), 3u);
        ASSERT_EQ(store.read(regions.global(instance, 5)).as_snapshot().count(), 4u);
    }

    TEST(D2TypedValues, a_translated_string_differs_when_only_its_locale_changes) {
        // arrange
        RegionTable regions;
        TypedStore store(regions);
        const SchemaId schema = regions.add_schema("Translated", InstanceKind::Component, std::vector<ValueType>{ValueType::TranslatedString});
        const InstanceId instance = store.create(schema);
        const PropertyId property = regions.global(instance, 0);

        // act: the payload is compared by shared pointer identity, as it is for a plain string (§13.3)
        const Value german = Value::make_translated("hallo", 1);
        store.write(property, german, WriteSource::GeneratedInit);
        const bool same_value = store.write(property, german, WriteSource::GeneratedInit);
        const bool other_locale = store.write(property, Value::make_translated("hallo", 2), WriteSource::GeneratedInit);
        const bool rebuilt = store.write(property, Value::make_translated("hallo", 2), WriteSource::GeneratedInit);

        // assert: the same value is not a change, and a different locale is (§13.1)
        ASSERT_FALSE(same_value);
        ASSERT_TRUE(other_locale);
        ASSERT_TRUE(rebuilt);
    }

    TEST(D2TypedValues, an_unchanged_typed_write_does_not_invalidate) {
        // arrange
        RegionTable regions;
        TypedStore store(regions);
        const SchemaId schema = regions.add_schema("Scalar", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        const InstanceId instance = store.create(schema);
        const PropertyId property = regions.global(instance, 0);

        // act
        store.write(property, Value(std::int64_t{3}), WriteSource::Binding);
        const bool changed = store.write(property, Value(std::int64_t{3}), WriteSource::Binding);

        // assert
        ASSERT_FALSE(changed);
    }

    TEST(D2TypedValues, a_snapshot_publish_of_equal_content_is_not_a_change) {
        // arrange
        RegionTable regions;
        TypedStore store(regions);
        const SchemaId schema = regions.add_schema("Snap", InstanceKind::Component, std::vector<ValueType>{ValueType::Snapshot});
        const InstanceId instance = store.create(schema);
        const PropertyId property = regions.global(instance, 0);

        // act
        store.write(property, Value::make_snapshot(std::make_shared<const std::vector<double>>(3, 2.0)), WriteSource::Binding);
        const bool equal_payload = store.write(property, Value::make_snapshot(std::make_shared<const std::vector<double>>(3, 2.0)), WriteSource::Binding);
        const bool different_payload = store.write(property, Value::make_snapshot(std::make_shared<const std::vector<double>>(3, 9.0)), WriteSource::Binding);

        // assert
        ASSERT_FALSE(equal_payload);
        ASSERT_TRUE(different_payload);
    }

    // ===================================================================================================================
    // deep chains
    // ===================================================================================================================

    TEST(D2Chains, eager_push_settles_a_chain_in_one_pass_per_layer) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const std::vector<PropertyId> chain = build_chain(*scene, schemas.single_int, 8);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().assign(chain.front(), Value(std::int64_t{7}));
        const SettleStats stats = scene->graph().settle();

        // assert
        ASSERT_EQ(stats.passes, 8u);
        ASSERT_EQ(stats.redundant_evaluations, 0u);
        ASSERT_EQ(scene->store().read(chain.back()).as_int(), 7);
    }

    TEST(D2Chains, the_pull_models_settle_a_deep_chain_in_one_pass) {
        // arrange
        for (const EvalModel model : {EvalModel::PullTopo, EvalModel::HybridPushPull}) {
            // arrange
            const auto scene = make_scene(model, false);
            const StandardSchemas schemas = declare_standard_schemas(scene->regions());
            const std::vector<PropertyId> chain = build_chain(*scene, schemas.single_int, 16);
            scene->graph().seal();
            static_cast<void>(scene->graph().settle());

            // act
            scene->graph().assign(chain.front(), Value(std::int64_t{5}));
            const SettleStats stats = scene->graph().settle();

            // assert
            ASSERT_EQ(stats.passes, 1u) << "model " << std::string(model_name(model));
            ASSERT_EQ(scene->store().read(chain.back()).as_int(), 5) << "model " << std::string(model_name(model));
        }
    }

    TEST(D2Chains, a_chain_value_reaches_the_tail_for_every_model) {
        // arrange
        for (const EvalModel model : {EvalModel::EagerPush, EvalModel::PullTopo, EvalModel::HybridPushPull}) {
            // arrange
            const auto scene = make_scene(model, false);
            const StandardSchemas schemas = declare_standard_schemas(scene->regions());
            const std::vector<PropertyId> chain = build_chain(*scene, schemas.single_int, 4);
            scene->graph().seal();
            static_cast<void>(scene->graph().settle());

            // act
            scene->graph().assign(chain.front(), Value(std::int64_t{42}));
            static_cast<void>(scene->graph().settle());

            // assert
            ASSERT_EQ(scene->store().read(chain.back()).as_int(), 42) << "model " << std::string(model_name(model));
        }
    }

    // ===================================================================================================================
    // diamond graphs
    // ===================================================================================================================

    TEST(D2Diamonds, a_simple_diamond_evaluates_its_sink_once) {
        // arrange
        for (const EvalModel model : {EvalModel::EagerPush, EvalModel::PullTopo, EvalModel::HybridPushPull}) {
            // arrange
            const auto scene = make_scene(model, false);
            const StandardSchemas schemas = declare_standard_schemas(scene->regions());
            const DiamondHandles<ErasedStore> handles = build_diamond(*scene, schemas.single_int, 2);
            scene->graph().seal();
            static_cast<void>(scene->graph().settle());

            // act
            scene->graph().assign(handles.source, Value(std::int64_t{6}));
            const SettleStats stats = scene->graph().settle();

            // assert
            ASSERT_EQ(stats.redundant_evaluations, 0u) << "model " << std::string(model_name(model));
            ASSERT_EQ(scene->store().read(handles.sink).as_int(), 12) << "model " << std::string(model_name(model));
        }
    }

    TEST(D2Diamonds, the_hybrid_model_evaluates_a_reconvergent_sink_once) {
        // arrange
        const auto scene = make_scene(EvalModel::HybridPushPull, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const DiamondHandles<ErasedStore> handles = build_reconvergent(*scene, schemas.single_int, 8, 8);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().assign(handles.source, Value(std::int64_t{3}));
        const SettleStats stats = scene->graph().settle();

        // assert
        ASSERT_EQ(stats.redundant_evaluations, 0u);
    }

    TEST(D2Diamonds, eager_push_re_evaluates_a_reconvergent_sink) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const DiamondHandles<ErasedStore> handles = build_reconvergent(*scene, schemas.single_int, 8, 8);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().assign(handles.source, Value(std::int64_t{3}));
        const SettleStats stats = scene->graph().settle();

        // assert: this is the behaviour the prototype exists to expose, so it is pinned as a regression test
        ASSERT_GT(stats.redundant_evaluations, 0u);
    }

    // ===================================================================================================================
    // dynamic dependency insertion and removal
    // ===================================================================================================================

    TEST(D2Mutation, an_added_dependency_starts_driving_its_binding) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());

        // create a source, a second source and a target bound only to the first (§4)
        const PropertyId first = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId second = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(first, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(second, Value(std::int64_t{5}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        const BindingId binding = scene->graph().add_binding(target, std::vector<PropertyId>{first}, &sum_all<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: give the binding a second dependency (§4)
        static_cast<void>(scene->graph().add_dependency(binding, second));
        scene->graph().assign(first, Value(std::int64_t{4}));
        static_cast<void>(scene->graph().settle());

        // assert: the target now sees both dependencies (§4)
        ASSERT_EQ(scene->store().read(target).as_int(), 9);
    }

    TEST(D2Mutation, a_removed_dependency_stops_driving_its_binding) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());

        // create a source, a second source and a target bound to both (§4)
        const PropertyId first = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId second = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(first, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(second, Value(std::int64_t{5}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        const BindingId binding = scene->graph().add_binding(target, std::vector<PropertyId>{first, second}, &sum_all<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: remove the second dependency (§4)
        static_cast<void>(scene->graph().remove_dependency(binding, second));
        scene->graph().assign(first, Value(std::int64_t{8}));
        static_cast<void>(scene->graph().settle());

        // assert: only the first dependency contributes now (§4)
        ASSERT_EQ(scene->store().read(target).as_int(), 8);
    }

    TEST(D2Mutation, a_removed_binding_is_never_evaluated_again) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());

        // create a source and two targets bound to it (§4)
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId kept = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId dropped = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(kept, Value(std::int64_t{0}), WriteSource::GeneratedInit);
        scene->store().write(dropped, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(kept, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        const BindingId doomed = scene->graph().add_binding(dropped, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        ASSERT_TRUE(scene->graph().remove_binding(doomed));
        scene->graph().assign(source, Value(std::int64_t{4}));
        const SettleStats stats = scene->graph().settle();

        // assert: only the surviving binding ran (§4)
        ASSERT_EQ(stats.bindings_evaluated, 1u);
        ASSERT_EQ(scene->store().read(kept).as_int(), 4);
        ASSERT_EQ(scene->store().read(dropped).as_int(), 1);
    }

    TEST(D2Mutation, the_segmented_index_removes_only_the_named_entry) {
        // arrange
        SegmentedIndex index;
        index.declare(0, std::vector<PropertyId>{7, 7});
        index.declare(1, std::vector<PropertyId>{7});

        // act
        const MutationCost cost = index.remove_entry(0, 7);

        // assert: one of the two entries for property 7 is gone, the other survives, and binding 1 is untouched (§4)
        ASSERT_EQ(index.dependents(7).size(), 2u);
        ASSERT_EQ(index.dependents(7)[0], 0u);
        ASSERT_EQ(index.dependents(7)[1], 1u);
        ASSERT_EQ(index.live_entries(), 2u);
        ASSERT_EQ(cost.shifted_entries, 2u);
    }

    // ===================================================================================================================
    // component-local PropertyIds
    // ===================================================================================================================

    TEST(D2ComponentLocalIds, component_local_ids_resolve_through_the_base_offset) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Pair", InstanceKind::Component, std::vector<ValueType>{ValueType::Int, ValueType::Int, ValueType::Int});

        // act
        const InstanceId first = regions.create(schema);
        const InstanceId second = regions.create(schema);

        // assert: both instances expose local ids 0..2 at different bases (AD-04a)
        ASSERT_EQ(regions.global(first, 0), 0u);
        ASSERT_EQ(regions.global(first, 2), 2u);
        ASSERT_EQ(regions.global(second, 0), 3u);
        ASSERT_EQ(regions.global(second, 2), 5u);
    }

    TEST(D2ComponentLocalIds, multiple_instances_of_one_component_keep_independent_values) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        ErasedStore store(regions);
        const InstanceId first = store.create(schema);
        const InstanceId second = store.create(schema);

        // act
        store.write(regions.global(first, 0), Value(std::int64_t{11}), WriteSource::GeneratedInit);
        store.write(regions.global(second, 0), Value(std::int64_t{22}), WriteSource::GeneratedInit);

        // assert
        ASSERT_EQ(store.read(regions.global(first, 0)).as_int(), 11);
        ASSERT_EQ(store.read(regions.global(second, 0)).as_int(), 22);
    }

    TEST(D2ComponentLocalIds, destroying_an_instance_keeps_other_ids_valid) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        ErasedStore store(regions);
        const InstanceId survivor = store.create(schema);
        const InstanceId doomed = store.create(schema);
        const PropertyId survivor_id = regions.global(survivor, 0);
        const PropertyId doomed_id = regions.global(doomed, 0);
        store.write(survivor_id, Value(std::int64_t{5}), WriteSource::GeneratedInit);

        // act
        store.destroy(doomed);

        // assert: the survivor is untouched and the destroyed one is reported dead (§5)
        ASSERT_EQ(regions.global(survivor, 0), survivor_id);
        ASSERT_EQ(store.read(survivor_id).as_int(), 5);
        ASSERT_FALSE(regions.alive(doomed));
    }

    TEST(D2ComponentLocalIds, a_destroyed_region_is_detected_as_stale_by_its_stamp) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        ErasedStore store(regions);
        const InstanceId instance = store.create(schema);
        const std::uint64_t stamp = regions.stamp(instance);

        // act
        store.destroy(instance);

        // assert: the stamp no longer resolves, so a holder of the old stamp can tell (§5)
        ASSERT_EQ(regions.instance_for_stamp(instance, stamp), INVALID_INSTANCE);
    }

    TEST(D2ComponentLocalIds, a_region_is_only_reused_by_a_matching_property_count) {
        // arrange
        RegionTable regions;
        const SchemaId single = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        const SchemaId wide = regions.add_schema("Wide", InstanceKind::Component, scalar_types(8));

        // act: free a wide region, then allocate a narrow one (§5)
        const InstanceId wide_instance = regions.create(wide);
        regions.destroy(wide_instance);
        regions.create(single);

        // assert: the mismatched region was not reused, so no id shifted (§5)
        ASSERT_EQ(regions.reuse().reused_same_size, 0u);
        ASSERT_GT(regions.reuse().rejected_mismatched_size, 0u);
    }

    TEST(D2ComponentLocalIds, a_region_of_a_matching_size_is_reused) {
        // arrange
        RegionTable regions;
        const SchemaId single = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

        // act
        const InstanceId first = regions.create(single);
        regions.destroy(first);
        const InstanceId second = regions.create(single);

        // assert: the free region came back, so the id space did not grow (§5)
        ASSERT_EQ(second, first);
        ASSERT_EQ(regions.reuse().reused_same_size, 1u);
    }

    // ===================================================================================================================
    // delegate, popup and instance churn
    // ===================================================================================================================

    TEST(D2InstanceKinds, delegate_instances_allocate_from_the_same_id_space) {
        // arrange
        RegionTable regions;
        const SchemaId component = regions.add_schema("Card", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        const SchemaId delegate = regions.add_schema("Row", InstanceKind::Delegate, std::vector<ValueType>{ValueType::Int});

        // act
        const InstanceId card = regions.create(component);
        const InstanceId row = regions.create(delegate);

        // assert: a delegate is a region like any other, and keeps its kind (§5)
        ASSERT_EQ(regions.schema(regions.region(row).schema).kind, InstanceKind::Delegate);
        ASSERT_EQ(regions.schema(regions.region(card).schema).kind, InstanceKind::Component);
        ASSERT_EQ(regions.live_instance_count(), 2u);
    }

    TEST(D2InstanceKinds, popup_instances_are_created_and_destroyed_without_disturbing_the_tree) {
        // arrange
        RegionTable regions;
        const SchemaId component = regions.add_schema("Card", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        const SchemaId popup = regions.add_schema("Popup", InstanceKind::Popup, std::vector<ValueType>{ValueType::Int});
        TypedStore store(regions);

        // create a standing tree of components (§5)
        std::vector<PropertyId> tree_ids;
        for (std::size_t index = 0; index < 16; index++) {
            const InstanceId instance = store.create(component);
            const PropertyId id = regions.global(instance, 0);
            store.write(id, Value(static_cast<std::int64_t>(index)), WriteSource::GeneratedInit);
            tree_ids.push_back(id);
        }

        // act: open and close popups over the standing tree (§5)
        for (std::size_t index = 0; index < 32; index++) {
            const InstanceId instance = store.create(popup);
            store.write(regions.global(instance, 0), Value(static_cast<std::int64_t>(index)), WriteSource::GeneratedInit);
            store.destroy(instance);
        }

        // assert: every standing value survived the popup churn (§5)
        for (std::size_t index = 0; index < tree_ids.size(); index++) {
            ASSERT_EQ(store.read(tree_ids.at(index)).as_int(), static_cast<std::int64_t>(index));
        }
    }

    TEST(D2InstanceKinds, a_delegate_binding_invalidation_reaches_its_own_subtree) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const SchemaId delegate = scene->regions().add_schema("Row", InstanceKind::Delegate, std::vector<ValueType>{ValueType::Int});

        // create a delegate source and a derived property that reads it (§5)
        const PropertyId delegate_source = scene->regions().global(scene->store().create(delegate), 0);
        const PropertyId derived = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(delegate_source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(derived, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(derived, std::vector<PropertyId>{delegate_source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().assign(delegate_source, Value(std::int64_t{9}));
        static_cast<void>(scene->graph().settle());

        // assert
        ASSERT_EQ(scene->store().read(derived).as_int(), 9);
    }

    // ===================================================================================================================
    // ReplaceBinding and binding removal
    // ===================================================================================================================

    TEST(D2ReplaceBinding, a_replacement_binding_takes_over_the_property) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId other = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(other, Value(std::int64_t{3}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        // act: bind, then replace the binding (§6)
        scene->graph().add_binding(target, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());
        ASSERT_EQ(scene->store().read(target).as_int(), 2);

        scene->graph().replace_binding(target, std::vector<PropertyId>{source, other}, &combine_two<ErasedStore>);
        static_cast<void>(scene->graph().settle());

        // assert: the new binding and its new dependencies are in force (§6)
        ASSERT_EQ(scene->store().read(target).as_int(), 23);
    }

    TEST(D2ReplaceBinding, the_replaced_binding_no_longer_participates) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source_a = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId source_b = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source_a, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(source_b, Value(std::int64_t{3}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(target, std::vector<PropertyId>{source_a}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: replace the binding with one that reads the other source (§6)
        scene->graph().replace_binding(target, std::vector<PropertyId>{source_b}, &pass_int<ErasedStore>);
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(source_a, Value(std::int64_t{99}));
        const SettleStats stats = scene->graph().settle();

        // assert: the old source no longer drives the property (§6)
        ASSERT_EQ(stats.bindings_evaluated, 0u);
        ASSERT_EQ(scene->store().read(target).as_int(), 3);
    }

    TEST(D2ReplaceBinding, replacing_twice_leaves_only_the_last_binding) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source_a = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId source_b = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source_a, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(source_b, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(target, std::vector<PropertyId>{source_a}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: replace twice (§6)
        scene->graph().replace_binding(target, std::vector<PropertyId>{source_b}, &pass_int<ErasedStore>);
        static_cast<void>(scene->graph().settle());
        scene->graph().replace_binding(target, std::vector<PropertyId>{source_a, source_b}, &combine_two<ErasedStore>);
        static_cast<void>(scene->graph().settle());

        // assert
        ASSERT_EQ(scene->store().read(target).as_int(), 12);
    }

    TEST(D2BindingRemoval, removing_a_binding_leaves_the_last_computed_value) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{4}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(target, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        static_cast<void>(scene->graph().remove_binding(scene->graph().writer_of(target)));
        static_cast<void>(scene->graph().settle());

        // assert: the value the binding last produced is retained (§6)
        ASSERT_EQ(scene->store().read(target).as_int(), 4);
    }

    // ===================================================================================================================
    // write precedence: imperative, binding, rebind
    // ===================================================================================================================

    TEST(D2Precedence, an_imperative_assignment_breaks_the_binding) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(target, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().assign(target, Value(std::int64_t{77}));
        scene->graph().assign(source, Value(std::int64_t{2}));
        static_cast<void>(scene->graph().settle());

        // assert: the binding no longer owns the property (§13.4)
        ASSERT_EQ(scene->store().read(target).as_int(), 77);
        ASSERT_FALSE(scene->graph().has_binding(target));
    }

    TEST(D2Precedence, rebind_restores_the_binding) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId target = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        const BindingId binding = scene->graph().add_binding(target, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(target, Value(std::int64_t{77}));
        static_cast<void>(scene->graph().settle());

        // act
        scene->graph().rebind(target);
        scene->graph().assign(source, Value(std::int64_t{5}));
        static_cast<void>(scene->graph().settle());

        // assert: the binding owns the property again (§13.4)
        ASSERT_EQ(scene->store().read(target).as_int(), 5);
        ASSERT_TRUE(scene->graph().has_binding(target));
        static_cast<void>(binding);
    }

    // ===================================================================================================================
    // cycle detection
    // ===================================================================================================================

    TEST(D2Cycles, a_static_cycle_is_detected) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId first = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId second = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(first, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(second, Value(std::int64_t{1}), WriteSource::GeneratedInit);

        // act
        scene->graph().add_binding(first, std::vector<PropertyId>{second}, &increment_first<ErasedStore>);
        scene->graph().add_binding(second, std::vector<PropertyId>{first}, &increment_first<ErasedStore>);
        scene->graph().seal();
        const CycleReport report = detect_cycles(scene->graph());

        // assert: Kahn cannot order a cycle (§7)
        ASSERT_TRUE(report.has_cycle);
        ASSERT_EQ(cycle_property_ids(scene->graph(), report).size(), 2u);
        ASSERT_FALSE(seal_proved_acyclic(scene->graph()));
    }

    TEST(D2Cycles, an_acyclic_graph_is_reported_clean) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const std::vector<PropertyId> chain = build_chain(*scene, schemas.single_int, 8);

        // act
        scene->graph().seal();
        const CycleReport report = detect_cycles(scene->graph());

        // assert
        ASSERT_FALSE(report.has_cycle);
        ASSERT_TRUE(seal_proved_acyclic(scene->graph()));
        ASSERT_EQ(scene->graph().ordered_binding_count(), chain.size() - 1);
    }

    TEST(D2Cycles, a_cycle_introduced_by_runtime_mutation_is_detected) {
        // arrange: a chain that is acyclic at seal time (§7)
        const auto scene = make_scene(EvalModel::EagerPush, true);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const std::vector<PropertyId> chain = build_chain(*scene, schemas.single_int, 3);
        scene->graph().seal();
        ASSERT_FALSE(detect_cycles(scene->graph()).has_cycle);

        // act: close the loop with a runtime dependency insertion (§7)
        static_cast<void>(scene->graph().add_dependency(0, chain.back()));

        // assert
        ASSERT_TRUE(detect_cycles(scene->graph()).has_cycle);
    }

    TEST(D2Cycles, a_non_converging_cycle_is_stopped_by_the_pass_budget) {
        // arrange: a cycle is kicked by an unbound source dependency, because an imperative write would break a binding
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId first = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId second = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(first, Value(std::int64_t{0}), WriteSource::GeneratedInit);
        scene->store().write(second, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        // act: bind a cycle that only ever increases, so it never reaches a fixpoint (§7)
        scene->graph().add_binding(first, std::vector<PropertyId>{second, source}, &offset_then_increment<ErasedStore>);
        scene->graph().add_binding(second, std::vector<PropertyId>{first}, &increment_first<ErasedStore>);
        scene->graph().seal();
        const SettleStats stats = scene->graph().settle();

        // assert: the budget stops it instead of hanging the UI thread (§28.2)
        ASSERT_TRUE(stats.cycle_budget_exhausted);
        ASSERT_EQ(stats.passes, MAX_SETTLE_PASSES);
    }

    TEST(D2Cycles, a_converging_cycle_settles_without_being_reported_as_exhausted) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId first = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId second = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{9}), WriteSource::GeneratedInit);
        scene->store().write(first, Value(std::int64_t{0}), WriteSource::GeneratedInit);
        scene->store().write(second, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        // act: bind a cycle that clamps into a fixed point instead of diverging (§7)
        scene->graph().add_binding(first, std::vector<PropertyId>{source, second}, &offset_then_clamp<ErasedStore>);
        scene->graph().add_binding(second, std::vector<PropertyId>{first}, &floor_first<ErasedStore>);
        scene->graph().seal();
        const SettleStats stats = scene->graph().settle();

        // assert: it converged, so the pass budget was never consumed (§7)
        ASSERT_FALSE(stats.cycle_budget_exhausted);
        ASSERT_EQ(scene->store().read(first).as_int(), 1);
        ASSERT_EQ(scene->store().read(second).as_int(), 1);
    }

    // ===================================================================================================================
    // animation shadowing
    // ===================================================================================================================

    TEST(D2Animation, an_animation_shadows_the_binding_value_without_breaking_it) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{2}), WriteSource::GeneratedInit);
        scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene->graph().settle());

        // assert: the animation owns the value and the binding is still attached (§13.5)
        ASSERT_EQ(scene->store().read(bound).as_int(), 100);
        ASSERT_FALSE(scene->graph().has_binding(bound));
        ASSERT_TRUE(scene->graph().binding_attached(bound));
        ASSERT_EQ(scene->store().source_of(bound), WriteSource::Animation);
    }

    TEST(D2Animation, ending_an_animation_yields_the_fresh_binding_value_when_dependencies_changed) {
        // arrange
        for (const AnimationEndMode mode : {AnimationEndMode::RestoreShadow, AnimationEndMode::ReEvaluate}) {
            // arrange
            const auto scene = make_scene(EvalModel::EagerPush, false);
            const StandardSchemas schemas = declare_standard_schemas(scene->regions());
            const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
            const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
            scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
            scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

            scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
            scene->graph().seal();
            static_cast<void>(scene->graph().settle());

            // act: animate, change the dependency under the animation, then end it (§8)
            static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
            static_cast<void>(scene->graph().settle());
            scene->graph().assign(source, Value(std::int64_t{55}));
            static_cast<void>(scene->graph().settle());
            scene->graph().end_animation(bound, mode);
            static_cast<void>(scene->graph().settle());

            // assert: the value comes from the binding, not from the shadow (§8)
            ASSERT_EQ(scene->store().read(bound).as_int(), 55) << "mode " << static_cast<int>(mode);
        }
    }

    TEST(D2Animation, restoring_the_shadow_without_invalidating_leaves_a_stale_value) {
        // arrange: this pins the defect that the invalidate in D's path is what prevents (§8)
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(source, Value(std::int64_t{55}));
        static_cast<void>(scene->graph().settle());
        scene->graph().end_animation(bound, AnimationEndMode::RestoreNoInvalidate);
        static_cast<void>(scene->graph().settle());

        // assert: the shadowed value from animation start is what survives (§8)
        ASSERT_EQ(scene->store().read(bound).as_int(), 1);
    }

    TEST(D2Animation, a_dependency_change_during_an_animation_is_visible_when_it_ends) {
        // arrange
        const auto scene = make_scene(EvalModel::HybridPushPull, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{3}), WriteSource::GeneratedInit);
        scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: the source moves twice under a running animation (§8)
        static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(source, Value(std::int64_t{7}));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(source, Value(std::int64_t{8}));
        static_cast<void>(scene->graph().settle());
        scene->graph().end_animation(bound, AnimationEndMode::ReEvaluate);
        static_cast<void>(scene->graph().settle());

        // assert: the last dependency change wins, not the value from animation start (§8)
        ASSERT_EQ(scene->store().read(bound).as_int(), 8);
    }

    TEST(D2Animation, an_imperative_write_during_an_animation_wins_and_ends_it) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act
        static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(bound, Value(std::int64_t{42}));
        static_cast<void>(scene->graph().settle());

        // assert: the explicit write took the property and the animation is over (AD-24)
        ASSERT_EQ(scene->store().read(bound).as_int(), 42);
        ASSERT_EQ(scene->store().source_of(bound), WriteSource::Imperative);
    }

    TEST(D2Animation, rebind_after_an_animation_re_evaluates_from_current_dependencies) {
        // arrange
        const auto scene = make_scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene->regions());
        const PropertyId source = scene->regions().global(scene->store().create(schemas.single_int), 0);
        const PropertyId bound = scene->regions().global(scene->store().create(schemas.single_int), 0);
        scene->store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);
        scene->store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        scene->graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
        scene->graph().seal();
        static_cast<void>(scene->graph().settle());

        // act: animate, move the source, override, then rebind (§8)
        static_cast<void>(scene->graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(source, Value(std::int64_t{60}));
        static_cast<void>(scene->graph().settle());
        scene->graph().assign(bound, Value(std::int64_t{1}));
        static_cast<void>(scene->graph().settle());
        scene->graph().rebind(bound);
        static_cast<void>(scene->graph().settle());

        // assert: the binding recomputes from the current source, not from the override (§8)
        ASSERT_EQ(scene->store().read(bound).as_int(), 60);
    }

    // ===================================================================================================================
    // string and snapshot values through a chain
    // ===================================================================================================================

    TEST(D2TypedChains, a_string_value_flows_through_a_typed_chain) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Str", InstanceKind::Component, std::vector<ValueType>{ValueType::String});
        TypedStore store(regions);
        Graph<TypedStore> graph(regions, store, EvalModel::EagerPush, false);

        // create a source and a derived property (§13.1)
        const InstanceId source_instance = store.create(schema);
        const InstanceId derived_instance = store.create(schema);
        const PropertyId source_property = regions.global(source_instance, 0);
        const PropertyId derived_property = regions.global(derived_instance, 0);
        store.write(source_property, Value::make_string("payload"), WriteSource::GeneratedInit);
        store.write(derived_property, Value::make_string(""), WriteSource::GeneratedInit);

        // act
        graph.add_binding(derived_property, std::vector<PropertyId>{source_property}, &pass_string<TypedStore>);
        graph.seal();
        static_cast<void>(graph.settle());

        // assert
        ASSERT_EQ(*store.read(derived_property).as_string(), "payload");
    }

} // namespace albor::prototype_d2
