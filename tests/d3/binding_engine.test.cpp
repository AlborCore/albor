// Prototype D3 — binding engine follow-up correctness (plan.md R3, R7)

#include <gtest/gtest.h>

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"

namespace
{
    using namespace albor::prototype_d2;

    // the realistic mix D3 measures (plan.md R2)
    const std::vector<ValueType>& mixed_types() {
        static const std::vector<ValueType> types{
            ValueType::Int, ValueType::Double, ValueType::Bool, ValueType::String, ValueType::TranslatedString, ValueType::Snapshot, ValueType::Int, ValueType::Double,
        };
        return types;
    }
} // namespace

// plan.md T3.3.4: the geometric growth policy exists to keep addresses stable, so every sampled sentinel
// must read back correctly after every growth event
TEST(D3Engine, GeometricSlabKeepsAddressesStableAcrossGrowth) {
    RegionTable regions;
    const SchemaId schema = regions.add_schema("Mixed", InstanceKind::Component, mixed_types());
    TypedStore store(regions);
    store.set_slab_growth(TypedStore::SlabGrowth::Geometric);

    // the seed instances whose addresses are observed
    constexpr std::size_t SEED = 64;
    std::vector<InstanceId> seed;
    std::vector<void*> addresses;
    for (std::size_t index = 0; index < SEED; index++) {
        const InstanceId instance = store.create(schema);
        seed.push_back(instance);
        addresses.push_back(store.storage_address_of(regions.global(instance, static_cast<LocalPropertyId>(0))));
    }

    // write a sentinel through the public path of every seed property
    for (std::size_t index = 0; index < SEED; index++) {
        static_cast<void>(store.write(regions.global(seed[index], static_cast<LocalPropertyId>(0)), Value(static_cast<std::int64_t>(static_cast<std::int64_t>(100) + static_cast<std::int64_t>(index))), WriteSource::GeneratedInit));
    }

    // force many growth events after the seed
    constexpr std::size_t PRESSURE = 4096;
    for (std::size_t index = 0; index < PRESSURE; index++) {
        static_cast<void>(store.create(schema));
    }

    // every observed address must still be the store's recorded address, and every sentinel must read back
    for (std::size_t index = 0; index < SEED; index++) {
        const PropertyId id = regions.global(seed[index], static_cast<LocalPropertyId>(0));
        EXPECT_EQ(addresses[index], store.storage_address_of(id)) << "property " << index << " moved";
        EXPECT_EQ(store.read(id).as_int(), static_cast<std::int64_t>(100) + static_cast<std::int64_t>(index)) << "sentinel " << index << " lost";
    }
}

// D2 recorded the fixed 64 KB chunk as its shipping behaviour; the default must stay exactly that
TEST(D3Engine, DefaultSlabGrowthChargesUniformChunks) {
    RegionTable regions;
    const SchemaId schema = regions.add_schema("Mixed", InstanceKind::Component, mixed_types());
    TypedStore store(regions);

    constexpr std::size_t INSTANCES = 8;
    for (std::size_t index = 0; index < INSTANCES; index++) {
        static_cast<void>(store.create(schema));
    }

    // one uniform chunk per the test double's own allocation count
    EXPECT_EQ(store.allocation_count(), 1);
    EXPECT_EQ(store.slab_allocated_bytes(), SLAB_CHUNK_BYTES);
}

// plan.md R7 scenario 3: with no shadow record at all, end_animation overwrites the imperative override;
// the active-set record is the minimal mechanism that keeps the imperative override standing
TEST(D3Engine, ActiveSetPreservesImperativeOverrideWhereNaiveEliminationDoesNot) {
    // the naive elimination, which must overwrite the override to prove the audit's point
    {
        RegionTable regions;
        const std::vector<ValueType> types{ValueType::Int, ValueType::Int};
        const SchemaId schema = regions.add_schema("Animated", InstanceKind::Component, types);
        TypedStore store(regions);
        const InstanceId instance = store.create(schema);
        const PropertyId source = regions.global(instance, static_cast<LocalPropertyId>(0));
        const PropertyId target = regions.global(instance, static_cast<LocalPropertyId>(1));

        Graph<TypedStore> graph(regions, store, EvalModel::EagerPush, false);
        graph.set_animation_shadowing(AnimationShadowing::None);
        const std::vector<PropertyId> dependencies{source};
        static_cast<void>(graph.add_binding(target, dependencies, [](BindingContext<TypedStore>& context) { context.write_int(context.read_int(context.declared().front()) + 1); }));
        graph.seal();

        graph.assign(source, Value(static_cast<std::int64_t>(10)));
        static_cast<void>(graph.settle());
        static_cast<void>(graph.set_animation(target, Value(static_cast<std::int64_t>(5555))));
        graph.assign(target, Value(static_cast<std::int64_t>(1234)));
        static_cast<void>(graph.settle());
        graph.end_animation(target, AnimationEndMode::RestoreShadow);
        static_cast<void>(graph.settle());

        // the naive elimination overwrites the override, which is the audit's failure
        EXPECT_EQ(graph.store().read(target).as_int(), 11);
        EXPECT_TRUE(graph.binding_attached(target));
    }

    // the active-set mechanism, which keeps the record without carrying a value
    {
        RegionTable regions;
        const std::vector<ValueType> types{ValueType::Int, ValueType::Int};
        const SchemaId schema = regions.add_schema("Animated", InstanceKind::Component, types);
        TypedStore store(regions);
        const InstanceId instance = store.create(schema);
        const PropertyId source = regions.global(instance, static_cast<LocalPropertyId>(0));
        const PropertyId target = regions.global(instance, static_cast<LocalPropertyId>(1));

        Graph<TypedStore> graph(regions, store, EvalModel::EagerPush, false);
        graph.set_animation_shadowing(AnimationShadowing::ActiveSet);
        const std::vector<PropertyId> dependencies{source};
        static_cast<void>(graph.add_binding(target, dependencies, [](BindingContext<TypedStore>& context) { context.write_int(context.read_int(context.declared().front()) + 1); }));
        graph.seal();

        graph.assign(source, Value(static_cast<std::int64_t>(10)));
        static_cast<void>(graph.settle());
        static_cast<void>(graph.set_animation(target, Value(static_cast<std::int64_t>(5555))));
        graph.assign(target, Value(static_cast<std::int64_t>(1234)));
        static_cast<void>(graph.settle());
        graph.end_animation(target, AnimationEndMode::RestoreShadow);
        static_cast<void>(graph.settle());

        // the imperative assignment survived the animation's end, which is the audit's pass
        EXPECT_EQ(graph.store().read(target).as_int(), 1234);
        EXPECT_FALSE(graph.binding_attached(target));
    }
}
