#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "prototypes/d/engine.h"
#include "prototypes/d/property_id.h"
#include "prototypes/d/store.h"
#include "prototypes/d/strategy.h"

namespace albor::prototype_d
{

    // fix the measured shape so that every strategy sees identical work (§30)
    struct WorkloadShape
    {
        std::size_t property_count = 100000; // §30 calls for about 10^5 properties
        std::size_t binding_count = 10000; // and about 10^4 bindings
        std::size_t node_count = 25000; // one node owns an input property plus its derived properties
        std::size_t chain_depth = 8; // each derived property feeds the next layer, which forces several passes
        std::size_t environment_properties = 64; // theme and locale properties that every node also reads (§13.1)
        std::size_t snapshot_properties = 8; // immutable application data arriving from a worker (§10.5)
        std::size_t animated_properties = 1024; // properties the animation layer drives every frame (§13.5)
    };

    // keep the driven property ids so that a run stays reproducible
    struct WorkloadHandles
    {
        std::vector<PropertyId> inputs;
        PropertyId theme = INVALID_PROPERTY;
        PropertyId locale = INVALID_PROPERTY;
        std::vector<PropertyId> snapshots;
        std::vector<PropertyId> animated;
        std::vector<PropertyId> tail_outputs;
    };

    // hold a ready store, a sealed engine and the driven property ids
    struct WorkloadScene
    {
        std::unique_ptr<PropertyStore> store;
        std::unique_ptr<BindingEngine> engine;
        WorkloadHandles handles;
    };

    // build the fixed scene on top of one invalidation strategy
    [[nodiscard]] WorkloadScene build_scene(const WorkloadShape& shape, std::unique_ptr<InvalidationStrategy> strategy);

    // run the recorded workload over the three candidates and print the measurements (§28.4)
    void run_benchmark();

    // build one immutable payload that stands in for worker data (§10.5)
    [[nodiscard]] std::shared_ptr<const std::vector<double>> make_snapshot(std::size_t sample_count);

    // publish a snapshot as a worker does through the thread safe path (§13.2)
    void publish_snapshot(WorkloadScene& scene, std::shared_ptr<const std::vector<double>> samples);

    // advance the animation layer by one frame, as the animation core would (§13.5)
    std::size_t advance_animation(WorkloadScene& scene, std::uint64_t frame);

} // namespace albor::prototype_d
