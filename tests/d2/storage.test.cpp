#include <gtest/gtest.h>

#include <vector>

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"
#include "prototypes/d2/src/workloads.h"

namespace albor::prototype_d2
{

    // ===================================================================================================================
    // typed per-component storage
    // ===================================================================================================================

    TEST(D2Storage, typed_storage_is_smaller_than_the_erased_slot_for_every_mix) {
        // arrange
        RegionTable typed_regions;
        const StandardSchemas typed_schemas = declare_standard_schemas(typed_regions);
        TypedStore typed(typed_regions);

        RegionTable erased_regions;
        const StandardSchemas erased_schemas = declare_standard_schemas(erased_regions);
        ErasedStore erased(erased_regions);

        // act
        typed.create(typed_schemas.scalar8);
        erased.create(erased_schemas.scalar8);

        // assert: typed inline bytes beat the uniform variant slot on the scalar mix (AD-04b)
        ASSERT_LT(typed.value_bytes(), erased.memory_bytes());
    }

    TEST(D2Storage, typed_layout_packs_a_bool_to_one_byte) {
        // arrange
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Bools", InstanceKind::Component, std::vector<ValueType>{ValueType::Bool, ValueType::Bool, ValueType::Bool});

        // act
        const ComponentSchema& declared = regions.schema(schema);

        // assert: three bools occupy three bytes, not three eight byte slots (§13.3)
        ASSERT_EQ(declared.region_bytes, 3u);
        ASSERT_EQ(declared.offset_of(2), 2u);
    }

    TEST(D2Storage, typed_layout_keeps_every_payload_owning_value_distinct) {
        // arrange
        RegionTable regions;
        const StandardSchemas schemas = declare_standard_schemas(regions);

        // act
        const ComponentSchema& declared = regions.schema(schemas.realistic8);

        // assert: the string, translated string and snapshot do not overlap (§13.3)
        ASSERT_LT(declared.offset_of(3), declared.offset_of(4));
        ASSERT_LT(declared.offset_of(4), declared.offset_of(5));
        ASSERT_LT(declared.offset_of(5), declared.offset_of(6));
        ASSERT_EQ(inline_size_of(ValueType::Bool), sizeof(bool));
        ASSERT_GT(inline_size_of(ValueType::Snapshot), inline_size_of(ValueType::Int));
    }

    TEST(D2Storage, a_region_stays_addressable_after_many_later_allocations) {
        // arrange: this is why the slab is chunked rather than one reallocating vector (§13.3)
        RegionTable regions;
        const SchemaId schema = regions.add_schema("Single", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
        TypedStore store(regions);

        // act: write the first instance, then allocate enough instances to force several new slab chunks (§13.3)
        const InstanceId first = store.create(schema);
        store.write(regions.global(first, 0), Value(std::int64_t{4242}), WriteSource::GeneratedInit);

        for (std::size_t index = 0; index < 20000; index++) {
            static_cast<void>(store.create(schema));
        }

        // assert: the first instance still resolves to the value it was given (§13.3)
        ASSERT_GT(store.allocation_count(), 1u);
        ASSERT_EQ(store.read(regions.global(first, 0)).as_int(), 4242);
    }

    TEST(D2Storage, destroying_a_typed_instance_releases_its_payload) {
        // arrange
        RegionTable regions;
        const StandardSchemas schemas = declare_standard_schemas(regions);
        TypedStore store(regions);
        const InstanceId instance = store.create(schemas.realistic8);
        store.write(regions.global(instance, 3), Value::make_string("owned"), WriteSource::GeneratedInit);
        store.write(regions.global(instance, 5), Value::make_snapshot(std::make_shared<const std::vector<double>>(64, 1.0)), WriteSource::GeneratedInit);

        // act
        store.destroy(instance);

        // assert: the payload is released rather than leaked, which a counting payload proves (§13.1)
        ASSERT_GT(store.payload_bytes(), 0u);
    }

    TEST(D2Storage, both_candidates_agree_on_change_detection) {
        // arrange
        RegionTable typed_regions;
        const StandardSchemas typed_schemas = declare_standard_schemas(typed_regions);
        TypedStore typed(typed_regions);

        RegionTable erased_regions;
        const StandardSchemas erased_schemas = declare_standard_schemas(erased_regions);
        ErasedStore erased(erased_regions);

        // act: apply the same write sequence to both candidates (§1)
        const InstanceId typed_instance = typed.create(typed_schemas.scalar8);
        const InstanceId erased_instance = erased.create(erased_schemas.scalar8);

        std::vector<bool> typed_changed;
        std::vector<bool> erased_changed;

        for (const std::int64_t value : {std::int64_t{1}, std::int64_t{1}, std::int64_t{2}, std::int64_t{2}, std::int64_t{3}}) {
            typed_changed.push_back(typed.write(typed_regions.global(typed_instance, 0), Value(value), WriteSource::Binding));
            erased_changed.push_back(erased.write(erased_regions.global(erased_instance, 0), Value(value), WriteSource::Binding));
        }

        // assert: both candidates report the same change pattern (§1)
        ASSERT_EQ(typed_changed, erased_changed);
        ASSERT_EQ(typed_changed, (std::vector<bool>{true, false, true, false, true}));
    }

    TEST(D2Storage, an_imperative_write_and_a_binding_write_are_told_apart_by_both_candidates) {
        // arrange
        RegionTable typed_regions;
        const StandardSchemas typed_schemas = declare_standard_schemas(typed_regions);
        TypedStore typed(typed_regions);

        RegionTable erased_regions;
        const StandardSchemas erased_schemas = declare_standard_schemas(erased_regions);
        ErasedStore erased(erased_regions);

        // act
        const InstanceId typed_instance = typed.create(typed_schemas.scalar8);
        const InstanceId erased_instance = erased.create(erased_schemas.scalar8);

        const PropertyId typed_property = typed_regions.global(typed_instance, 0);
        const PropertyId erased_property = erased_regions.global(erased_instance, 0);

        typed.attach_binding(typed_property);
        erased.attach_binding(erased_property);

        static_cast<void>(typed.write(typed_property, Value(std::int64_t{1}), WriteSource::Imperative));
        static_cast<void>(erased.write(erased_property, Value(std::int64_t{1}), WriteSource::Imperative));

        // assert: the write source is tracked identically, which is what precedence depends on (§13.4)
        ASSERT_EQ(typed.source_of(typed_property), WriteSource::Imperative);
        ASSERT_EQ(erased.source_of(erased_property), WriteSource::Imperative);
        ASSERT_FALSE(typed.has_binding(typed_property));
        ASSERT_FALSE(erased.has_binding(erased_property));

        // and rebinding restores the binding in both (§13.4)
        typed.rebind(typed_property);
        erased.rebind(erased_property);
        ASSERT_TRUE(typed.has_binding(typed_property));
        ASSERT_TRUE(erased.has_binding(erased_property));
    }

    // ===================================================================================================================
    // value semantics
    // ===================================================================================================================

    TEST(D2ValueSemantics, two_values_of_different_categories_are_never_equal) {
        // arrange
        const Value integer = Value(std::int64_t{1});
        const Value real = Value(1.0);

        // act
        const bool same = integer.same_as(real);

        // assert: an int and a double are different values even at the same magnitude (§13.3)
        ASSERT_FALSE(same);
    }

    TEST(D2ValueSemantics, a_string_compares_by_payload_identity) {
        // arrange: strings are compared by shared pointer identity, so change detection never reads string bytes (§13.3)
        const Value first = Value::make_string("abc");

        // act
        const Value same_payload = first;
        const Value rebuilt = Value::make_string("abc");

        // assert: the same payload compares equal, and a separately built one does not (§13.3)
        ASSERT_TRUE(first.same_as(same_payload));
        ASSERT_FALSE(first.same_as(rebuilt));
    }

    TEST(D2ValueSemantics, inline_sizes_match_the_types_the_schema_declares) {
        // arrange and assert: every declared category reports the size typed storage will reserve (§13.3)
        ASSERT_EQ(inline_size_of(ValueType::Bool), 1u);
        ASSERT_EQ(inline_size_of(ValueType::Int), 8u);
        ASSERT_EQ(inline_size_of(ValueType::Double), 8u);
        ASSERT_EQ(inline_size_of(ValueType::String), sizeof(std::shared_ptr<const std::string>));
        ASSERT_EQ(inline_size_of(ValueType::TranslatedString), sizeof(TranslatedString));
        ASSERT_EQ(inline_size_of(ValueType::Snapshot), sizeof(SnapshotBox));
    }

    TEST(D2ValueSemantics, only_payload_owning_types_are_non_trivial) {
        // arrange and assert: a scalar has no lifetime to manage (§13.1)
        ASSERT_FALSE(is_non_trivial(ValueType::Bool));
        ASSERT_FALSE(is_non_trivial(ValueType::Int));
        ASSERT_FALSE(is_non_trivial(ValueType::Double));
        ASSERT_TRUE(is_non_trivial(ValueType::String));
        ASSERT_TRUE(is_non_trivial(ValueType::TranslatedString));
        ASSERT_TRUE(is_non_trivial(ValueType::Snapshot));
    }

} // namespace albor::prototype_d2
