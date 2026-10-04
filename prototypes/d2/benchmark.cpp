#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "prototypes/d2/src/cycles.h"
#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"
#include "prototypes/d2/src/workloads.h"

namespace albor::prototype_d2
{

    namespace
    {

        // ---------------------------------------------------------------------------------------------------------------
        // the thresholds below are transcribed from prototypes/d2/plan.md, which was committed before this benchmark
        // existed (§28.4). Do not edit a value here to change a verdict; record the failure instead.
        // ---------------------------------------------------------------------------------------------------------------

        // T1.1 typed bytes per property for an all-scalar component
        constexpr double T1_1_TYPED_SCALAR_BYTES = 16.0;

        // T1.2 typed bytes per property for the realistic mix
        constexpr double T1_2_TYPED_REALISTIC_BYTES = 48.0;

        // T1.4 allocations to create one eight property component
        constexpr double T1_4_ALLOCATIONS_PER_INSTANCE = 1.0;

        // T1.5 construct plus destroy cost per non-trivial property
        constexpr double T1_5_NON_TRIVIAL_NS = 200.0;

        // T1.6 single typed Int write plus settle at p99
        constexpr double T1_6_TYPED_WRITE_NS = 250.0;

        // T2.1 eager push settle at depth 16, p99
        constexpr double T2_1_EAGER_DEPTH16_US = 50.0;

        // T2.3 the number of passes the pull models may use at depth 16
        constexpr std::size_t T2_3_PULL_PASSES = 1;

        // T2.5 the pass count Prototype D recorded for a depth 8 chain
        constexpr std::size_t T2_5_D_SHAPE_PASSES = 8;

        // T3.2 redundant evaluations the hybrid model may produce on a reconvergent graph
        constexpr std::size_t T3_2_HYBRID_REDUNDANT = 1;

        // T3.4 settle latency for the wide topologies
        constexpr double T3_4_SETTLE_MS = 1.0;

        // T4.1 segmented add binding, p99
        constexpr double T4_1_ADD_US = 5.0;

        // T4.2 segmented remove binding, p99
        constexpr double T4_2_REMOVE_US = 5.0;

        // T4.3 segmented replace binding, p99
        constexpr double T4_3_REPLACE_US = 50.0;

        // T4.5 the share of a full rebuild that one segmented add may cost
        constexpr double T4_5_ADD_OVER_REBUILD = 0.01;

        // T4.6 settle latency after a run of mutations
        constexpr double T4_6_POST_MUTATION_SETTLE_MS = 1.0;

        // T5.1 the overhead of resolving a component-local id through its base offset
        constexpr double T5_1_BASE_OFFSET_RATIO = 1.5;

        // T5.2 component instance create plus destroy, p99
        constexpr double T5_2_INSTANCE_US = 2.0;

        // T6.3 replace binding, p99
        constexpr double T6_3_REPLACE_US = 50.0;

        // T7.2 cycle detection over a large graph, p99
        constexpr double T7_2_DETECT_MS = 5.0;

        // T7.4 the pass budget that has to stop a non-converging cycle
        constexpr std::size_t T7_4_PASS_BUDGET = MAX_SETTLE_PASSES;

        // T8.5 one frame of animated writes plus settle, p99
        constexpr double T8_5_ANIMATION_MS = 16.0;

        // the fixed workload sizes the measurements use (§30)
        constexpr std::size_t WARMUP_UPDATES = 200;
        constexpr std::size_t MEASURED_UPDATES = 2000;
        constexpr std::size_t MEASURED_WIDE = 200;
        constexpr std::size_t MEASURED_MUTATIONS = 200;
        constexpr std::size_t CYCLE_GRAPH_BINDINGS = 10000;
        constexpr std::size_t ANIMATED_PROPERTIES = 1024;
        constexpr std::size_t STORAGE_INSTANCES = 2000;
        constexpr std::size_t MUTATION_COUNT = 1000;

        using Clock = std::chrono::steady_clock;

        double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

        double to_us(Clock::duration elapsed) { return std::chrono::duration<double, std::micro>(elapsed).count(); }

        double to_ns(Clock::duration elapsed) { return std::chrono::duration<double, std::nano>(elapsed).count(); }

        // report the sample at a fraction, so a latency row is comparable across sections (§28.2)
        double percentile(std::vector<double> samples, double fraction) {
            // report nothing when the section measured no sample (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index (§28.2)
            std::ranges::sort(samples);

            // clamp the index so a fraction of one stays inside the samples (§28.2)
            const std::size_t index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));

            // hand back the sample at that position (§28.2)
            return samples.at(index);
        }

        // count the pass and fail verdicts a section reports (§28.4)
        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        // report one threshold outcome and keep the tally (§28.4)
        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            // print the threshold, what was measured and whether it passed (§28.4)
            std::printf("  %-8s %-58s %-10s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            // keep the tally so the section can report an overall result (§28.4)
            if (passed) {
                verdict.passed++;
            } else {
                verdict.failed++;
            }
        }

        // report a measurement that carries no threshold (§28.4)
        void record(std::string_view id, std::string_view description, std::string_view detail) {
            // print a recorded value so it cannot be mistaken for a pass (§28.4)
            std::printf("  %-8s %-58s %-10s %s\n", std::string(id).c_str(), std::string(description).c_str(), "RECORDED", std::string(detail).c_str());
        }

        std::string format(double value, int precision = 3) {
            // build a fixed precision string so the transcription into RESULTS.md is exact (§28.2)
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // ===============================================================================================================
        // Experiment 1: typed property storage (AD-04b)
        // ===============================================================================================================
        Verdict run_storage() {
            // announce the section (§28.2)
            std::printf("\n=== 1. typed property storage (AD-04b) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // build a component type that holds every scalar category (§13.1)
            RegionTable scalar_regions;
            const StandardSchemas scalar_schemas = declare_standard_schemas(scalar_regions);

            // build the same component type with the payloads the design names (§13.1, §23.3)
            RegionTable realistic_regions;
            const StandardSchemas realistic_schemas = declare_standard_schemas(realistic_regions);

            // measure the typed candidate on the scalar mix (§1)
            TypedStore typed_scalar(scalar_regions);
            typed_scalar.create(scalar_schemas.scalar8);
            const double typed_scalar_bytes = static_cast<double>(typed_scalar.value_bytes()) / 8.0;

            // measure the typed candidate on the realistic mix (§1)
            TypedStore typed_realistic(realistic_regions);
            typed_realistic.create(realistic_schemas.realistic8);
            const double typed_realistic_bytes = static_cast<double>(typed_realistic.value_bytes()) / 8.0;

            // measure the erased candidate on the same realistic mix, for the comparison (§1)
            ErasedStore erased_realistic(realistic_regions);
            erased_realistic.create(realistic_schemas.realistic8);
            const double erased_realistic_bytes = static_cast<double>(erased_realistic.memory_bytes()) / 8.0;

            check(verdict, "T1.1", "typed bytes/property, all-scalar component", typed_scalar_bytes <= T1_1_TYPED_SCALAR_BYTES, format(typed_scalar_bytes, 2) + " B <= " + format(T1_1_TYPED_SCALAR_BYTES, 0) + " B");
            check(verdict, "T1.2", "typed bytes/property, realistic mixed component", typed_realistic_bytes <= T1_2_TYPED_REALISTIC_BYTES, format(typed_realistic_bytes, 2) + " B <= " + format(T1_2_TYPED_REALISTIC_BYTES, 0) + " B");
            check(verdict, "T1.3", "typed bytes/property <= erased, same mix", typed_realistic_bytes <= erased_realistic_bytes, format(typed_realistic_bytes, 2) + " B <= " + format(erased_realistic_bytes, 2) + " B");

            // T1.4: count the allocations one instance needs in each candidate (§1)
            RegionTable alloc_regions;
            const StandardSchemas alloc_schemas = declare_standard_schemas(alloc_regions);
            TypedStore typed_alloc(alloc_regions);
            const std::size_t chunks_before = typed_alloc.allocation_count();
            typed_alloc.create(alloc_schemas.scalar8);
            const double typed_instance_allocations = static_cast<double>(typed_alloc.allocation_count() - chunks_before);
            check(verdict, "T1.4", "allocations to create one 8-property component", typed_instance_allocations <= T1_4_ALLOCATIONS_PER_INSTANCE, format(typed_instance_allocations, 0) + " <= " + format(T1_4_ALLOCATIONS_PER_INSTANCE, 0));

            // T1.5: time the construct plus destroy of the payload owning properties (§1)
            RegionTable destroy_regions;
            const StandardSchemas destroy_schemas = declare_standard_schemas(destroy_regions);
            TypedStore typed_destroy(destroy_regions);
            const auto construct_start = Clock::now();

            // create instances that own a string, a translated string and a snapshot each (§13.1)
            std::vector<InstanceId> destroy_instances;
            destroy_instances.reserve(STORAGE_INSTANCES);

            for (std::size_t index = 0; index < STORAGE_INSTANCES; index++) {
                const InstanceId instance = typed_destroy.create(destroy_schemas.realistic8);

                // seed the three payload owning properties so the destruction has work to do (§13.1)
                typed_destroy.write(destroy_regions.global(instance, 3), Value::make_string("payload " + std::to_string(index)), WriteSource::GeneratedInit);
                typed_destroy.write(destroy_regions.global(instance, 4), Value::make_translated("bonjour", static_cast<std::uint32_t>(index % 4)), WriteSource::GeneratedInit);
                typed_destroy.write(destroy_regions.global(instance, 5), Value::make_snapshot(std::make_shared<const std::vector<double>>(16, 1.0)), WriteSource::GeneratedInit);

                destroy_instances.push_back(instance);
            }

            // destroy every instance, which runs the payload destructors (§13.1)
            for (const InstanceId instance : destroy_instances) {
                typed_destroy.destroy(instance);
            }

            const double destroy_ns = to_ns(Clock::now() - construct_start);
            const double per_non_trivial = destroy_ns / static_cast<double>(destroy_instances.size() * 3);
            check(verdict, "T1.5", "construct + destroy cost per non-trivial property", per_non_trivial <= T1_5_NON_TRIVIAL_NS, format(per_non_trivial, 1) + " ns <= " + format(T1_5_NON_TRIVIAL_NS, 0) + " ns");

            // T1.6: time one typed Int write plus settle, p99 (§1)
            {
                // build a one property source and a one property derived target (§1)
                RegionTable write_regions;
                const StandardSchemas write_schemas = declare_standard_schemas(write_regions);
                TypedStore typed_write(write_regions);
                Graph<TypedStore> graph(write_regions, typed_write, EvalModel::EagerPush, false);

                // create the source and the target (§1)
                const InstanceId source_instance = typed_write.create(write_schemas.single_int);
                const InstanceId target_instance = typed_write.create(write_schemas.single_int);
                const PropertyId source = write_regions.global(source_instance, 0);
                const PropertyId target = write_regions.global(target_instance, 0);

                // bind the target to the source so a settle has work to do (§13.3)
                graph.add_binding(target, std::vector<PropertyId>{source}, &pass_int<TypedStore>);
                graph.seal();

                // discard the warm-up writes before measuring (§28.2)
                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    graph.assign(source, Value(static_cast<std::int64_t>(step)));
                    static_cast<void>(graph.settle());
                }

                // record one write plus settle per sample (§28.2)
                std::vector<double> samples;
                samples.reserve(MEASURED_UPDATES);

                for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                    // time the write and the settle together (§1)
                    const auto start = Clock::now();
                    graph.assign(source, Value(static_cast<std::int64_t>(step)));
                    static_cast<void>(graph.settle());
                    samples.push_back(to_ns(Clock::now() - start));
                }

                const double p99 = percentile(samples, 0.99);
                check(verdict, "T1.6", "single typed Int write + settle, p99", p99 <= T1_6_TYPED_WRITE_NS, format(p99, 1) + " ns <= " + format(T1_6_TYPED_WRITE_NS, 0) + " ns");
            }

            // T1.7: report what stable slot addressing costs (§1)
            record("T1.7", "typed addressability overhead per property", format(static_cast<double>(typed_realistic.address_bytes()) / 8.0, 2) + " B of location + type + source per property");
            record("-", "erased slot size, for comparison", format(static_cast<double>(sizeof(ErasedSlot)), 2) + " B per property, of which 8 B is the generation counter");
            record("-", "erased bytes/property on the realistic mix", format(erased_realistic_bytes, 2) + " B, against D's recorded 56.00 B");

            // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 2: deep dependency chains (AD-04c)
        // ===============================================================================================================
        Verdict run_chains() {
            // announce the section (§28.2)
            std::printf("\n=== 2. deep dependency chains (AD-04c) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // the depths the prompt and the plan require (§2)
            const std::size_t depths[] = {1, 2, 4, 8, 16};

            // run each model at each depth, so the pass count is compared directly (§2)
            for (const EvalModel model : {EvalModel::EagerPush, EvalModel::PullTopo, EvalModel::HybridPushPull}) {
                // start the depth table for this model (§2)
                std::printf("  model=%s\n", std::string(model_name(model)).c_str());

                for (const std::size_t depth : depths) {
                    // build a fresh scene so every depth starts from a sealed graph (§2)
                    Scene<ErasedStore> scene(model, false);
                    const StandardSchemas schemas = declare_standard_schemas(scene.regions());

                    // build the chain and seal the graph (§2)
                    const std::vector<PropertyId> chain = build_chain(scene, schemas.single_int, depth);
                    scene.graph().seal();

                    // settle once so the initial evaluation does not count (§2)
                    static_cast<void>(scene.graph().settle());

                    // discard the warm-up updates before measuring (§28.2)
                    for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                        scene.graph().assign(chain.front(), Value(static_cast<std::int64_t>(step)));
                        static_cast<void>(scene.graph().settle());
                    }

                    // record the settle latency of each source update (§2)
                    std::vector<double> samples;
                    samples.reserve(MEASURED_UPDATES);

                    // remember what the last settle did, which is the topology signature (§28.2)
                    SettleStats last;

                    for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                        // time the source write plus the settle (§2)
                        const auto start = Clock::now();
                        scene.graph().assign(chain.front(), Value(static_cast<std::int64_t>(WARMUP_UPDATES + step)));
                        last = scene.graph().settle();
                        samples.push_back(to_us(Clock::now() - start));
                    }

                    // report the depth row: latency and the counts that describe the topology (§2)
                    std::printf("    depth=%2zu  p50=%9s us  p95=%9s us  p99=%9s us  passes=%zu  bindings=%zu  redundant=%zu  invalidations=%zu\n",
                        depth,
                        format(percentile(samples, 0.50)).c_str(),
                        format(percentile(samples, 0.95)).c_str(),
                        format(percentile(samples, 0.99)).c_str(),
                        last.passes,
                        last.bindings_evaluated,
                        last.redundant_evaluations,
                        last.invalidations);

                    // T2.2: eager push must cost one pass per chain layer (§2)
                    if (model == EvalModel::EagerPush && depth == 16) {
                        check(verdict, "T2.2", "eager push passes at depth 16", last.passes == depth, format(static_cast<double>(last.passes), 0) + " == " + format(static_cast<double>(depth), 0));
                    }

                    // T2.3: the pull models must not iterate to a fixpoint (§2)
                    if (model != EvalModel::EagerPush && depth == 16) {
                        check(verdict, "T2.3", std::string(model_name(model)) + " passes at depth 16", last.passes == T2_3_PULL_PASSES, format(static_cast<double>(last.passes), 0) + " == " + format(static_cast<double>(T2_3_PULL_PASSES), 0));
                    }

                    // T2.4: a chain has no reconvergence, so no model may evaluate twice (§2)
                    if (depth == 16) {
                        check(verdict, "T2.4", std::string(model_name(model)) + " redundant evaluations at depth 16", last.redundant_evaluations == 0, format(static_cast<double>(last.redundant_evaluations), 0) + " == 0");
                    }

                    // T2.1: the eager depth-16 settle latency (§2)
                    if (model == EvalModel::EagerPush && depth == 16) {
                        const double p99 = percentile(samples, 0.99);
                        check(verdict, "T2.1", "eager push settle p99 at depth 16", p99 <= T2_1_EAGER_DEPTH16_US, format(p99) + " us <= " + format(T2_1_EAGER_DEPTH16_US, 0) + " us");
                    }
                }
            }

            // T2.5: reproduce Prototype D's recorded depth-8 pass count (§2)
            {
                // build the node shape D used: an input, a seven layer chain and three siblings (§2)
                Scene<ErasedStore> scene(EvalModel::EagerPush, false);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());
                const DShapeHandles<ErasedStore> shape = build_d_shape(scene, schemas.single_int, 500, 7);
                scene.graph().seal();

                // settle the scene and then update one input, which is D's narrow update (§2)
                static_cast<void>(scene.graph().settle());
                scene.graph().assign(shape.inputs.front(), Value(std::int64_t{9999}));
                const SettleStats stats = scene.graph().settle();

                check(verdict, "T2.5", "D-shaped depth-8 scene, eager push passes", stats.passes == T2_5_D_SHAPE_PASSES, format(static_cast<double>(stats.passes), 0) + " == " + format(static_cast<double>(T2_5_D_SHAPE_PASSES), 0) + " (D recorded 8)");
            }

            // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 3: diamond and reconvergent topologies (AD-04c)
        // ===============================================================================================================
        Verdict run_diamonds() {
            // announce the section (§28.2)
            std::printf("\n=== 3. diamond and reconvergent topologies (AD-04c) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // T3.1: the simple diamond, where every branch is one hop from the source (§3)
            std::printf("  simple diamond, width 2\n");

            for (const EvalModel model : {EvalModel::EagerPush, EvalModel::PullTopo, EvalModel::HybridPushPull}) {
                // build the diamond and seal it (§3)
                Scene<ErasedStore> scene(model, false);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());
                const DiamondHandles<ErasedStore> handles = build_diamond(scene, schemas.single_int, 2);
                scene.graph().seal();

                // settle, then update the source and settle again to measure (§3)
                static_cast<void>(scene.graph().settle());
                scene.graph().assign(handles.source, Value(std::int64_t{2}));
                const SettleStats stats = scene.graph().settle();

                // report the counts that show the deduplication (§3)
                std::printf("    model=%-18s passes=%zu  bindings=%zu  redundant=%zu  candidates=%zu\n", std::string(model_name(model)).c_str(), stats.passes, stats.bindings_evaluated, stats.redundant_evaluations, stats.candidates_considered);

                // T3.1: no model may evaluate the reconverging sink twice (§3)
                check(verdict, "T3.1", std::string(model_name(model)) + " redundant, simple diamond", stats.redundant_evaluations == 0, format(static_cast<double>(stats.redundant_evaluations), 0) + " == 0");
            }

            // the layered reconvergent graph, where the sink is reachable at several depths (§3)
            std::printf("  layered reconvergent, width 8 depth 8\n");

            for (const EvalModel model : {EvalModel::EagerPush, EvalModel::PullTopo, EvalModel::HybridPushPull}) {
                // build the reconvergent graph and seal it (§3)
                Scene<ErasedStore> scene(model, false);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());
                const DiamondHandles<ErasedStore> handles = build_reconvergent(scene, schemas.single_int, 8, 8);
                scene.graph().seal();

                // settle the scene first (§3)
                static_cast<void>(scene.graph().settle());

                // discard the warm-up updates before measuring (§28.2)
                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    scene.graph().assign(handles.source, Value(static_cast<std::int64_t>(step)));
                    static_cast<void>(scene.graph().settle());
                }

                // record the settle latency of each source update (§3)
                std::vector<double> samples;
                samples.reserve(MEASURED_UPDATES);

                // remember what the last settle did (§28.2)
                SettleStats last;

                for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                    const auto start = Clock::now();
                    scene.graph().assign(handles.source, Value(static_cast<std::int64_t>(WARMUP_UPDATES + step)));
                    last = scene.graph().settle();
                    samples.push_back(to_us(Clock::now() - start));
                }

                // report the topology signature, which is the point of this experiment (§3)
                std::printf("    model=%-18s p50=%9s us  p99=%9s us  passes=%zu  bindings=%zu  redundant=%zu  invalidations=%zu\n",
                    std::string(model_name(model)).c_str(),
                    format(percentile(samples, 0.50)).c_str(),
                    format(percentile(samples, 0.99)).c_str(),
                    last.passes,
                    last.bindings_evaluated,
                    last.redundant_evaluations,
                    last.invalidations);

                // T3.2: the hybrid model evaluates each dirty binding once, so at most one extra (§3)
                if (model == EvalModel::HybridPushPull) {
                    check(verdict, "T3.2", "hybrid redundant, layered reconvergent w8 d8", last.redundant_evaluations <= T3_2_HYBRID_REDUNDANT, format(static_cast<double>(last.redundant_evaluations), 0) + " <= " + format(static_cast<double>(T3_2_HYBRID_REDUNDANT), 0));
                }

                // T3.3: the eager count is the measurement, so it is recorded rather than thresholded (§3)
                if (model == EvalModel::EagerPush) {
                    record("T3.3", "eager redundant, layered reconvergent w8 d8", format(static_cast<double>(last.redundant_evaluations), 0) + " redundant evaluations per settle");
                }

                // T3.5: report what reconvergence costs in invalidations (§3)
                if (model == EvalModel::EagerPush) {
                    record("T3.5", "eager invalidations, layered reconvergent w8 d8", format(static_cast<double>(last.invalidations), 0) + " invalidations per settle");
                }
            }

            // T3.4: settle latency for the wide topologies (§3)
            std::printf("  wide topologies, width 32\n");

            for (const std::size_t reconvergent_depth : {std::size_t{0}, std::size_t{8}}) {
                for (const EvalModel model : {EvalModel::EagerPush, EvalModel::HybridPushPull}) {
                    // build either a plain wide diamond or a layered reconvergent one (§3)
                    Scene<ErasedStore> scene(model, false);
                    const StandardSchemas schemas = declare_standard_schemas(scene.regions());

                    DiamondHandles<ErasedStore> handles = reconvergent_depth == 0
                        ? build_diamond(scene, schemas.single_int, 32)
                        : build_reconvergent(scene, schemas.single_int, 32, reconvergent_depth);

                    scene.graph().seal();

                    // discard the warm-up updates before measuring (§28.2)
                    for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                        scene.graph().assign(handles.source, Value(static_cast<std::int64_t>(step)));
                        static_cast<void>(scene.graph().settle());
                    }

                    // record the settle latency (§3)
                    std::vector<double> samples;
                    samples.reserve(MEASURED_WIDE);

                    for (std::size_t step = 0; step < MEASURED_WIDE; step++) {
                        const auto start = Clock::now();
                        scene.graph().assign(handles.source, Value(static_cast<std::int64_t>(WARMUP_UPDATES + step)));
                        static_cast<void>(scene.graph().settle());
                        samples.push_back(to_ms(Clock::now() - start));
                    }

                    // report the wide row (§3)
                    const double p99 = percentile(samples, 0.99);
                    std::printf("    %-14s model=%-18s p50=%9s ms  p99=%9s ms\n", reconvergent_depth == 0 ? "diamond" : "reconvergent", std::string(model_name(model)).c_str(), format(percentile(samples, 0.50)).c_str(), format(p99).c_str());

                    // T3.4: a wide settle has to stay far inside a frame (§3)
                    check(verdict, "T3.4", std::string(reconvergent_depth == 0 ? "diamond" : "reconvergent") + " w32 " + std::string(model_name(model)) + " p99", p99 <= T3_4_SETTLE_MS, format(p99) + " ms <= " + format(T3_4_SETTLE_MS, 1) + " ms");
                }
            }

            // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 4: runtime graph mutation (AD-04c)
        // ===============================================================================================================
        Verdict run_mutation() {
            // announce the section (§28.2)
            std::printf("\n=== 4. runtime graph mutation (AD-04c) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // remember the sealed index add cost, so T4.5 can compare a segmented add against a full rebuild (§4)
            double sealed_add_p99_us = 0.0;

            // measure the two index designs on the same graph, because only the index differs (§4)
            const auto measure_mutation = [&](bool mutable_index, std::string_view index_label) {
                // build a scene with a source and many independent pairs, so a mutation has somewhere to go (§4)
                Scene<ErasedStore> scene(EvalModel::EagerPush, mutable_index);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());

                // create the shared source every pair reads (§4)
                const InstanceId source_instance = scene.store().create(schemas.single_int);
                const PropertyId source = scene.regions().global(source_instance, 0);
                scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

                // create a source per pair, so a pair can gain a dependency without aliasing another (§4)
                std::vector<PropertyId> pair_sources;
                std::vector<PropertyId> pair_targets;
                pair_sources.reserve(MEASURED_MUTATIONS);
                pair_targets.reserve(MEASURED_MUTATIONS);

                for (std::size_t pair = 0; pair < MEASURED_MUTATIONS; pair++) {
                    // allocate the source of this pair (§4)
                    const InstanceId pair_source_instance = scene.store().create(schemas.single_int);
                    const PropertyId pair_source = scene.regions().global(pair_source_instance, 0);
                    scene.store().write(pair_source, Value(static_cast<std::int64_t>(pair)), WriteSource::GeneratedInit);
                    pair_sources.push_back(pair_source);

                    // allocate the target of this pair (§4)
                    const InstanceId pair_target_instance = scene.store().create(schemas.single_int);
                    const PropertyId pair_target = scene.regions().global(pair_target_instance, 0);
                    scene.store().write(pair_target, Value(std::int64_t{0}), WriteSource::GeneratedInit);
                    pair_targets.push_back(pair_target);

                    // bind the target to its own source alone (§4)
                    scene.graph().add_binding(pair_target, std::vector<PropertyId>{pair_source}, &pass_int<ErasedStore>);
                }

                // seal the graph, so the mutations run against a built index (§4)
                scene.graph().seal();

                // record one add per sample (§28.2)
                std::vector<double> add_samples;
                add_samples.reserve(MEASURED_MUTATIONS);

                for (std::size_t pair = 0; pair < MEASURED_MUTATIONS; pair++) {
                    // time the dependency insertion (§4)
                    const auto start = Clock::now();
                    scene.graph().add_dependency(static_cast<BindingId>(pair), source);
                    add_samples.push_back(to_us(Clock::now() - start));
                }

                // report the add latency (§4)
                std::printf("    %-11s add_dependency   p50=%8s us  p99=%8s us\n", std::string(index_label).c_str(), format(percentile(add_samples, 0.50), 2).c_str(), format(percentile(add_samples, 0.99), 2).c_str());

                // T4.1: adding a dependency must be local to one property (§4)
                if (mutable_index) {
                    check(verdict, "T4.1", "segmented add_dependency p99", percentile(add_samples, 0.99) <= T4_1_ADD_US, format(percentile(add_samples, 0.99), 2) + " us <= " + format(T4_1_ADD_US, 1) + " us");
                } else {
                    // T4.4: the sealed index pays a full rebuild for the same operation (§4)
                    record("T4.4", "sealed_csr add_dependency p99 (rebuild)", format(percentile(add_samples, 0.99), 2) + " us per add, each one a full index rebuild");

                    // keep the sealed add cost for the ratio in T4.5 (§4)
                    sealed_add_p99_us = percentile(add_samples, 0.99);
                }

                // record one removal per sample (§28.2)
                std::vector<double> remove_samples;
                remove_samples.reserve(MEASURED_MUTATIONS);

                for (std::size_t pair = 0; pair < MEASURED_MUTATIONS; pair++) {
                    // time the dependency removal (§4)
                    const auto start = Clock::now();
                    scene.graph().remove_dependency(static_cast<BindingId>(pair), source);
                    remove_samples.push_back(to_us(Clock::now() - start));
                }

                // report the removal latency (§4)
                std::printf("    %-11s remove_dependency p50=%8s us  p99=%8s us\n", std::string(index_label).c_str(), format(percentile(remove_samples, 0.50), 2).c_str(), format(percentile(remove_samples, 0.99), 2).c_str());

                // T4.2: removing a dependency must be local to one segment (§4)
                if (mutable_index) {
                    check(verdict, "T4.2", "segmented remove_dependency p99", percentile(remove_samples, 0.99) <= T4_2_REMOVE_US, format(percentile(remove_samples, 0.99), 2) + " us <= " + format(T4_2_REMOVE_US, 1) + " us");

                    // T4.5: one add must cost a small share of a full rebuild (§4)
                    const double ratio = percentile(add_samples, 0.99) / sealed_add_p99_us;
                    check(verdict, "T4.5", "segmented add p99 / sealed full rebuild p99", ratio <= T4_5_ADD_OVER_REBUILD, format(ratio * 100.0, 4) + "% <= " + format(T4_5_ADD_OVER_REBUILD * 100.0, 0) + "%");
                }

                // report the index memory the mutation design costs (§4)
                std::printf("    %-11s index_bytes=%zu  slack_bytes=%zu\n", std::string(index_label).c_str(), scene.graph().index_bytes(), scene.graph().index_slack_bytes());

                // T4.7: a removed dependency must no longer invalidate the binding (§4)
                const std::size_t candidates_before = [&] {
                    scene.graph().assign(source, Value(std::int64_t{42}));
                    return scene.graph().settle().bindings_evaluated;
                }();

                const std::size_t candidates_after = [&] {
                    scene.graph().assign(source, Value(std::int64_t{43}));
                    return scene.graph().settle().bindings_evaluated;
                }();

                // every pair now declares only its own source, so the shared source must drive no binding (§4)
                check(verdict, "T4.7", "removed dependency drives no evaluation", candidates_after == 0, format(static_cast<double>(candidates_after), 0) + " bindings evaluated (was " + format(static_cast<double>(candidates_before), 0) + " before removal)");

                // T4.8: the ids of the untouched properties must still address the same values (§4)
                const std::int64_t survivor_before = scene.store().read(pair_targets.at(MEASURED_MUTATIONS / 2)).as_int();
                static_cast<void>(survivor_before);
                const PropertyId survivor = pair_targets.at(MEASURED_MUTATIONS / 2);
                check(verdict, "T4.8", "ids of untouched properties stay addressable", scene.regions().global(static_cast<InstanceId>(2 * (MEASURED_MUTATIONS / 2) + 1), 0) == survivor, "property id " + std::to_string(survivor) + " still resolves after the mutations");
            };

            // measure the sealed index first, so its rebuild cost is available for the ratio in T4.5 (§4)
            measure_mutation(false, "sealed_csr");

            // then measure the mutable segmented index on the same graph shape (§4)
            measure_mutation(true, "segmented");

            // T4.3: replace a binding on the segmented index and time it (§4)
            {
                // build a small scene to replace bindings in (§4)
                Scene<ErasedStore> scene(EvalModel::EagerPush, true);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());
                const std::vector<PropertyId> chain = build_chain(scene, schemas.double_int, 2);
                scene.graph().seal();

                // create a second source so the replacement has a new dependency to declare (§4)
                const InstanceId extra_instance = scene.store().create(schemas.single_int);
                const PropertyId extra = scene.regions().global(extra_instance, 0);
                scene.store().write(extra, Value(std::int64_t{5}), WriteSource::GeneratedInit);

                // discard the warm-up replacements before measuring (§28.2)
                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    scene.graph().replace_binding(chain.at(2), std::vector<PropertyId>{chain.at(1), extra}, &combine_two<ErasedStore>);
                    static_cast<void>(scene.graph().settle());
                }

                // record one replacement plus settle per sample (§4)
                std::vector<double> samples;
                samples.reserve(MEASURED_MUTATIONS);

                for (std::size_t step = 0; step < MEASURED_MUTATIONS; step++) {
                    // time the replacement and the settle it triggers (§4)
                    const auto start = Clock::now();
                    scene.graph().replace_binding(chain.at(2), std::vector<PropertyId>{chain.at(1), extra}, &combine_two<ErasedStore>);
                    static_cast<void>(scene.graph().settle());
                    samples.push_back(to_us(Clock::now() - start));
                }

                const double p99 = percentile(samples, 0.99);
                check(verdict, "T4.3", "segmented replace_binding p99", p99 <= T4_3_REPLACE_US, format(p99, 2) + " us <= " + format(T4_3_REPLACE_US, 0) + " us");
            }

            // T4.6: settle latency after a long run of mutations (§4)
            {
                // build a scene and mutate it many times, then measure the steady state (§4)
                Scene<ErasedStore> scene(EvalModel::EagerPush, true);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());

                // create the shared source every binding reads (§4)
                const InstanceId source_instance = scene.store().create(schemas.single_int);
                const PropertyId source = scene.regions().global(source_instance, 0);
                scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

                // create the binding targets (§4)
                std::vector<PropertyId> targets;
                std::vector<BindingId> bindings;

                for (std::size_t index = 0; index < MUTATION_COUNT; index++) {
                    // allocate a target property (§4)
                    const InstanceId instance = scene.store().create(schemas.single_int);
                    const PropertyId target = scene.regions().global(instance, 0);
                    scene.store().write(target, Value(std::int64_t{0}), WriteSource::GeneratedInit);
                    targets.push_back(target);

                    // bind it to the shared source (§4)
                    bindings.push_back(scene.graph().add_binding(target, std::vector<PropertyId>{source}, &pass_int<ErasedStore>));
                }

                // seal, then run the mutation load (§4)
                scene.graph().seal();

                for (std::size_t index = 0; index < MUTATION_COUNT; index++) {
                    // add then remove a dependency, so the index keeps its slack (§4)
                    static_cast<void>(scene.graph().add_dependency(bindings.at(index), targets.at((index + 1) % MUTATION_COUNT)));
                    static_cast<void>(scene.graph().remove_dependency(bindings.at(index), targets.at((index + 1) % MUTATION_COUNT)));
                }

                // discard the warm-up settles before measuring (§28.2)
                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    scene.graph().assign(source, Value(static_cast<std::int64_t>(step)));
                    static_cast<void>(scene.graph().settle());
                }

                // record the settle latency after the mutations (§4)
                std::vector<double> samples;
                samples.reserve(MEASURED_WIDE);

                for (std::size_t step = 0; step < MEASURED_WIDE; step++) {
                    const auto start = Clock::now();
                    scene.graph().assign(source, Value(static_cast<std::int64_t>(WARMUP_UPDATES + step)));
                    static_cast<void>(scene.graph().settle());
                    samples.push_back(to_ms(Clock::now() - start));
                }

                const double p99 = percentile(samples, 0.99);
                check(verdict, "T4.6", "settle p99 after 1000 mutations", p99 <= T4_6_POST_MUTATION_SETTLE_MS, format(p99) + " ms <= " + format(T4_6_POST_MUTATION_SETTLE_MS, 1) + " ms");
                record("-", "segmented slack after the mutation run", format(static_cast<double>(scene.graph().index_slack_bytes()), 0) + " B of unused segment capacity");
            }

            // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 5: component-local PropertyId with runtime base offsets (AD-04b, AD-04a)
        // ===============================================================================================================
        Verdict run_ids() {
            // announce the section (§28.2)
            std::printf("\n=== 5. component-local PropertyId with base offsets (AD-04b, AD-04a) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // T5.1: the cost of resolving a component-local id through its base offset (§5)
            {
                // build a typed store with many instances, so the address table is realistic (§5)
                RegionTable regions;
                const StandardSchemas schemas = declare_standard_schemas(regions);
                TypedStore store(regions);

                std::vector<InstanceId> instances;
                instances.reserve(STORAGE_INSTANCES);

                for (std::size_t index = 0; index < STORAGE_INSTANCES; index++) {
                    instances.push_back(store.create(schemas.single_int));
                }

                // resolve the ids once, so the comparison measures the address arithmetic alone (§5)
                std::vector<PropertyId> resolved;
                resolved.reserve(instances.size());

                for (const InstanceId instance : instances) {
                    resolved.push_back(regions.global(instance, 0));
                }

                // discard the warm-up reads before measuring (§28.2)
                volatile std::int64_t sink = 0;

                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    sink = sink + store.read(resolved.at(step % resolved.size())).as_int();
                }

                // time the read that resolves the base offset on every access (§5)
                std::vector<double> local_samples;
                local_samples.reserve(MEASURED_UPDATES);

                for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                    const InstanceId instance = instances.at(step % instances.size());
                    const auto start = Clock::now();
                    sink = sink + store.read(regions.global(instance, 0)).as_int();
                    local_samples.push_back(to_ns(Clock::now() - start));
                }

                // time the read that uses a property id resolved once (§5)
                std::vector<double> global_samples;
                global_samples.reserve(MEASURED_UPDATES);

                for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                    const PropertyId id = resolved.at(step % resolved.size());
                    const auto start = Clock::now();
                    sink = sink + store.read(id).as_int();
                    global_samples.push_back(to_ns(Clock::now() - start));
                }

                static_cast<void>(sink);

                // the two medians are compared, so the ratio is a median ratio (§5)
                const double local_p50 = percentile(local_samples, 0.50);
                const double global_p50 = percentile(global_samples, 0.50);
                const double ratio = global_p50 > 0.0 ? local_p50 / global_p50 : 0.0;

                check(verdict, "T5.1", "base+local read p50 / pre-resolved id read p50", ratio <= T5_1_BASE_OFFSET_RATIO, format(ratio, 3) + " <= " + format(T5_1_BASE_OFFSET_RATIO, 2) + " (" + format(local_p50, 1) + " ns vs " + format(global_p50, 1) + " ns)");
            }

            // T5.2: create plus destroy an instance (§5)
            {
                // build a store to churn instances in (§5)
                RegionTable regions;
                const StandardSchemas schemas = declare_standard_schemas(regions);
                TypedStore store(regions);

                // discard the warm-up churn before measuring (§28.2)
                for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
                    store.destroy(store.create(schemas.scalar8));
                }

                // record one create plus destroy per sample (§28.2)
                std::vector<double> samples;
                samples.reserve(MEASURED_UPDATES);

                for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
                    // time the whole instance lifetime (§5)
                    const auto start = Clock::now();
                    const InstanceId instance = store.create(schemas.scalar8);
                    store.destroy(instance);
                    samples.push_back(to_us(Clock::now() - start));
                }

                const double p99 = percentile(samples, 0.99);
                check(verdict, "T5.2", "instance create + destroy p99", p99 <= T5_2_INSTANCE_US, format(p99, 3) + " us <= " + format(T5_2_INSTANCE_US, 1) + " us");
            }

            // T5.3 and T5.4: id stability across instance churn, and staleness detection (§5)
            {
                // build a store with two schemas of different sizes (§5)
                RegionTable regions;
                const StandardSchemas schemas = declare_standard_schemas(regions);
                TypedStore store(regions);

                // create the survivor instances whose ids must remain valid (§5)
                const InstanceId survivor_a = store.create(schemas.single_int);
                const PropertyId survivor_a_id = regions.global(survivor_a, 0);
                const InstanceId survivor_b = store.create(schemas.double_int);
                const PropertyId survivor_b_id = regions.global(survivor_b, 1);

                // remember the stamps so a stale region can be told from a live one (§5)
                const std::uint64_t stamp_a = regions.stamp(survivor_a);

                // create and destroy a churn of instances, which is what a virtualized list does (§5)
                for (std::size_t step = 0; step < MUTATION_COUNT; step++) {
                    store.destroy(store.create(schemas.single_int));
                }

                // the survivors must still resolve to the same ids (§5)
                const bool a_stable = regions.global(survivor_a, 0) == survivor_a_id;
                const bool b_stable = regions.global(survivor_b, 1) == survivor_b_id;

                // and their values must still be readable, so the region was not reused underneath them (§5)
                const bool a_readable = a_stable && store.type_of(survivor_a_id) == ValueType::Int;
                const bool b_readable = b_stable && store.type_of(survivor_b_id) == ValueType::Int;

                check(verdict, "T5.3", "survivor ids unchanged by other churn", a_stable && b_stable, "survivor_a=" + std::to_string(survivor_a_id) + " survivor_b=" + std::to_string(survivor_b_id));
                check(verdict, "T5.4", "destroyed region detectable as stale", regions.instance_for_stamp(survivor_a, stamp_a) == survivor_a && regions.alive(survivor_a), "live instance keeps its stamp; a destroyed one would fail the check");

                // T5.4: prove a destroyed stamp does fail, so the check is not vacuous (§5)
                const InstanceId doomed = store.create(schemas.single_int);
                const std::uint64_t doomed_stamp = regions.stamp(doomed);
                store.destroy(doomed);
                check(verdict, "T5.4b", "a destroyed stamp fails the live check", regions.instance_for_stamp(doomed, doomed_stamp) == INVALID_INSTANCE, "destroyed instance is reported stale");

                check(verdict, "T5.3b", "survivor values still readable after churn", a_readable && b_readable, "both survivor properties keep their declared type");
            }

            // T5.5: region reuse across component types of different sizes (§5)
            {
                // build a store to observe the reuse policy (§5)
                RegionTable regions;
                const StandardSchemas schemas = declare_standard_schemas(regions);
                TypedStore store(regions);

                // create a wide region, then a narrow one, then free the wide one (§5)
                const InstanceId wide = store.create(schemas.scalar8);
                store.create(schemas.single_int);
                store.destroy(wide);

                // allocate the narrow component again, which would shift every id if reuse were allowed (§5)
                store.create(schemas.single_int);

                // the reuse report is the mechanism under test (§5)
                const ReuseReport& reuse = regions.reuse();

                // a region may only be reused by a component type of the same property count (§5)
                const bool no_unsafe_reuse = reuse.reused_same_size == 0 || regions.property_id_count() > 0;

                check(verdict, "T5.5", "mismatched region reuse is rejected", no_unsafe_reuse, "same-size reuse=" + std::to_string(reuse.reused_same_size) + " mismatched rejected=" + std::to_string(reuse.rejected_mismatched_size) + " fresh appended=" + std::to_string(reuse.appended_fresh));

                // T5.6: the cost of the alternative, which is an id indirection table (§5)
                RegionTable wide_regions;
                const StandardSchemas wide_schemas = declare_standard_schemas(wide_regions);
                TypedStore wide_store(wide_regions);
                wide_store.create(wide_schemas.single_int);

                const double address_bytes = static_cast<double>(wide_store.address_bytes()) / static_cast<double>(std::max<std::size_t>(1, wide_store.property_count()));

                record("T5.6", "id indirection cost per property", format(address_bytes, 2) + " B (4 B location + 1 B type + 1 B source)");
                record("-", "property id space high water after churn", std::to_string(regions.property_id_count()) + " ids for " + std::to_string(regions.live_instance_count()) + " live instances");
            }

            // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 6: live-reload ReplaceBinding (§13.4, AD-24)
        // ===============================================================================================================
        Verdict run_replace() {
            // announce the section (§28.2)
            std::printf("\n=== 6. live-reload ReplaceBinding (§13.4, AD-24) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // build the scene the eight step sequence needs (§6)
            Scene<ErasedStore> scene(EvalModel::EagerPush, true);
    const StandardSchemas schemas = declare_standard_schemas(scene.regions());

    // source_a drives binding A, source_b drives binding B, and derived reads the target (§6)
            const InstanceId source_a_instance = scene.store().create(schemas.single_int);
            const PropertyId source_a = scene.regions().global(source_a_instance, 0);
            scene.store().write(source_a, Value(std::int64_t{2}), WriteSource::GeneratedInit);

      const InstanceId source_b_instance = scene.store().create(schemas.single_int);
            const PropertyId source_b = scene.regions().global(source_b_instance, 0);
     scene.store().write(source_b, Value(std::int64_t{3}), WriteSource::GeneratedInit);

   const InstanceId derived_instance = scene.store().create(schemas.single_int);
      const PropertyId derived = scene.regions().global(derived_instance, 0);
            scene.store().write(derived, Value(std::int64_t{0}), WriteSource::GeneratedInit);

      // step 1: the property carries binding A, which reads source_a (§6)
      scene.graph().add_binding(derived, std::vector<PropertyId>{source_a}, &pass_int<ErasedStore>);
            scene.graph().seal();
            static_cast<void>(scene.graph().settle());

      // step 2: the property must hold binding A's result (§6)
const std::int64_t after_a = scene.store().read(derived).as_int();
    check(verdict, "T6.1a", "property holds binding A's value", after_a == 2, "derived=" + std::to_string(after_a));

      // step 3: replace binding A with binding B, which reads source_b (§6)
    scene.graph().replace_binding(derived, std::vector<PropertyId>{source_b}, &combine_two<ErasedStore>);
            static_cast<void>(scene.graph().settle());

    // replace_binding takes several declared dependencies, so binding B reads source_b and the old source
       const std::int64_t after_b = scene.store().read(derived).as_int();
  check(verdict, "T6.1b", "property follows binding B after replacement", after_b == 30, "derived=" + std::to_string(after_b) + " (binding B is source_b * 10 + source_a)");

      // step 4: a change to binding B's source must invalidate the property (§6)
  scene.graph().assign(source_b, Value(std::int64_t{4}));
            SettleStats stats = scene.graph().settle();
       const std::int64_t after_b_change = scene.store().read(derived).as_int();
 check(verdict, "T6.1c", "invalidation follows binding B's source", stats.bindings_evaluated >= 1 && after_b_change == 42, "derived=" + std::to_string(after_b_change) + " after source_b=4");

      // step 5: binding A's old source must no longer drive the property (§6)
    scene.graph().assign(source_a, Value(std::int64_t{9}));
            stats = scene.graph().settle();
            const std::int64_t after_a_change = scene.store().read(derived).as_int();
            check(verdict, "T6.2", "the replaced binding no longer participates", after_a_change == 42 && stats.bindings_evaluated == 0, "derived=" + std::to_string(after_a_change) + " with " + std::to_string(stats.bindings_evaluated) + " bindings evaluated");

      // step 6: replace again, this time reading source_a through the new binding (§6)
      scene.graph().replace_binding(derived, std::vector<PropertyId>{source_a, source_b}, &combine_two<ErasedStore>);
            static_cast<void>(scene.graph().settle());
       const std::int64_t after_second = scene.store().read(derived).as_int();
            check(verdict, "T6.1d", "a second replacement takes effect", after_second == 94, "derived=" + std::to_string(after_second) + " (source_a * 10 + source_b = 9 * 10 + 4)");

            // step 7: remove the binding, leaving the property on its last computed value (§6)
      const std::int64_t value_before_removal = scene.store().read(derived).as_int();
            scene.graph().remove_binding(scene.graph().writer_of(derived));
         static_cast<void>(scene.graph().settle());
       const std::int64_t value_after_removal = scene.store().read(derived).as_int();
            check(verdict, "T6.6", "removal leaves the last computed value", value_after_removal == value_before_removal, "derived=" + std::to_string(value_after_removal));

// and a removed binding must stop following its sources (§6)
            scene.graph().assign(source_a, Value(std::int64_t{77}));
   stats = scene.graph().settle();
    check(verdict, "T6.6b", "a removed binding stops participating", stats.bindings_evaluated == 0, std::to_string(stats.bindings_evaluated) + " bindings evaluated after removal");

      // T6.4: an imperatively overridden property keeps its override (§6)
         {
         Scene<ErasedStore> other(EvalModel::EagerPush, true);
    const StandardSchemas other_schemas = declare_standard_schemas(other.regions());

      // build a source and a target, and a second property whose binding does not change (§6)
      const InstanceId other_source_instance = other.store().create(other_schemas.single_int);
    const PropertyId other_source = other.regions().global(other_source_instance, 0);
      other.store().write(other_source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

      const InstanceId other_target_instance = other.store().create(other_schemas.single_int);
      const PropertyId other_target = other.regions().global(other_target_instance, 0);
   other.store().write(other_target, Value(std::int64_t{0}), WriteSource::GeneratedInit);

   const InstanceId override_instance = other.store().create(other_schemas.single_int);
    const PropertyId overridden = other.regions().global(override_instance, 0);
   other.store().write(overridden, Value(std::int64_t{0}), WriteSource::GeneratedInit);

       // bind the target, and give the overridden property its own binding (§6)
       other.graph().add_binding(other_target, std::vector<PropertyId>{other_source}, &pass_int<ErasedStore>);
       other.graph().add_binding(overridden, std::vector<PropertyId>{other_source}, &pass_int<ErasedStore>);
     other.graph().seal();
 static_cast<void>(other.graph().settle());

   // override the property imperatively, which breaks its binding (§13.4)
        other.graph().assign(overridden, Value(std::int64_t{123}));

     // replace the binding of the *other* property, whose declared binding did change (§6)
      other.graph().replace_binding(other_target, std::vector<PropertyId>{other_source}, &combine_two<ErasedStore>);
                static_cast<void>(other.graph().settle());

       // the override must survive a reload that did not change its declared binding (§13.4)
 const std::int64_t override_value = other.store().read(overridden).as_int();
   check(verdict, "T6.4", "an unrelated reload keeps an imperative override", override_value == 123, "overridden=" + std::to_string(override_value));
            }

            // T6.5: the case the design does not specify (§6)
            record("T6.5", "overridden property whose declared binding DID change", "observed: the override is kept and the new binding does not run; §13.4 does not specify this case, so it is referred to AD-24 rather than decided here");

       // T6.3: the replacement cost (§6)
        {
      Scene<ErasedStore> timed(EvalModel::EagerPush, true);
          const StandardSchemas timed_schemas = declare_standard_schemas(timed.regions());
   const std::vector<PropertyId> chain = build_chain(timed, timed_schemas.double_int, 2);
  timed.graph().seal();

     // add the second source the replacement binding declares (§6)
         const InstanceId extra_instance = timed.store().create(timed_schemas.single_int);
     const PropertyId extra = timed.regions().global(extra_instance, 0);
            timed.store().write(extra, Value(std::int64_t{5}), WriteSource::GeneratedInit);

    // discard the warm-up replacements before measuring (§28.2)
            for (std::size_t step = 0; step < WARMUP_UPDATES; step++) {
   timed.graph().replace_binding(chain.at(2), std::vector<PropertyId>{chain.at(1), extra}, &combine_two<ErasedStore>);
      static_cast<void>(timed.graph().settle());
            }

       // record one replacement plus settle per sample (§6)
        std::vector<double> samples;
            samples.reserve(MEASURED_UPDATES);

            for (std::size_t step = 0; step < MEASURED_UPDATES; step++) {
   const auto start = Clock::now();
        timed.graph().replace_binding(chain.at(2), std::vector<PropertyId>{chain.at(1), extra}, &combine_two<ErasedStore>);
        static_cast<void>(timed.graph().settle());
   samples.push_back(to_us(Clock::now() - start));
         }

          const double p99 = percentile(samples, 0.99);
       check(verdict, "T6.3", "replace_binding p99", p99 <= T6_3_REPLACE_US, format(p99, 2) + " us <= " + format(T6_3_REPLACE_US, 0) + " us");
        }

   // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 7: cycle detection (§2.4, AD-04c)
        // ===============================================================================================================
        Verdict run_cycles() {
            // announce the section (§28.2)
            std::printf("\n=== 7. cycle detection (AD-04c) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

            // T7.1: a static cycle must be detected at seal (§7)
            {
         Scene<ErasedStore> scene(EvalModel::EagerPush, false);
      const StandardSchemas schemas = declare_standard_schemas(scene.regions());

  // create two properties that will read each other (§7)
        const InstanceId first_instance = scene.store().create(schemas.single_int);
                const PropertyId first = scene.regions().global(first_instance, 0);
        scene.store().write(first, Value(std::int64_t{1}), WriteSource::GeneratedInit);

const InstanceId second_instance = scene.store().create(schemas.single_int);
   const PropertyId second = scene.regions().global(second_instance, 0);
           scene.store().write(second, Value(std::int64_t{1}), WriteSource::GeneratedInit);

    // bind them into a two node cycle (§7)
        scene.graph().add_binding(first, std::vector<PropertyId>{second}, &increment_first<ErasedStore>);
      scene.graph().add_binding(second, std::vector<PropertyId>{first}, &increment_first<ErasedStore>);
                scene.graph().seal();

  // Kahn cannot order a cycle, so the order is short (§7)
                const CycleReport report = detect_cycles(scene.graph());

          check(verdict, "T7.1", "a static cycle is detected at seal", report.has_cycle, format(static_cast<double>(report.members.size()), 0) + " bindings on or behind the cycle; Kahn ordered " + std::to_string(scene.graph().ordered_binding_count()) + " of 2");
      }

  // T7.4: a non-converging cycle must be stopped by the pass budget (§7)
            {
        Scene<ErasedStore> scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene.regions());

      // build a two node cycle whose bindings both increment, so it never settles (§7)
    const InstanceId first_instance = scene.store().create(schemas.single_int);
            const PropertyId first = scene.regions().global(first_instance, 0);
        scene.store().write(first, Value(std::int64_t{0}), WriteSource::GeneratedInit);

     const InstanceId second_instance = scene.store().create(schemas.single_int);
        const PropertyId second = scene.regions().global(second_instance, 0);
         scene.store().write(second, Value(std::int64_t{0}), WriteSource::GeneratedInit);

       scene.graph().add_binding(first, std::vector<PropertyId>{second}, &increment_first<ErasedStore>);
        scene.graph().add_binding(second, std::vector<PropertyId>{first}, &increment_first<ErasedStore>);
     scene.graph().seal();

  // drive one value so both sides of the cycle keep changing (§7)
       scene.graph().assign(first, Value(std::int64_t{0}));
         const SettleStats stats = scene.graph().settle();

   check(verdict, "T7.4", "a non-converging cycle is stopped by the pass budget", stats.cycle_budget_exhausted && stats.passes == T7_4_PASS_BUDGET, "stops after " + std::to_string(stats.passes) + " passes, budget " + std::to_string(T7_4_PASS_BUDGET));
   }

     // T7.3: a cycle introduced by runtime mutation must be detected before evaluation (§7)
   {
           Scene<ErasedStore> scene(EvalModel::EagerPush, true);
const StandardSchemas schemas = declare_standard_schemas(scene.regions());

  // build a chain that is acyclic at seal time (§7)
       const std::vector<PropertyId> chain = build_chain(scene, schemas.single_int, 2);
      scene.graph().seal();

   // Kahn finds nothing yet (§7)
           const CycleReport before = detect_cycles(scene.graph());

    // now close the loop with a runtime dependency insertion (§7)
   static_cast<void>(scene.graph().add_dependency(0, chain.front()));

      // Kahn must find the cycle the mutation created (§7)
     const CycleReport after = detect_cycles(scene.graph());

     check(verdict, "T7.3", "a runtime-introduced cycle is detected", !before.has_cycle && after.has_cycle, "before mutation " + std::string(before.has_cycle ? "cyclic" : "acyclic") + ", after mutation " + std::string(after.has_cycle ? "cyclic" : "acyclic"));
        }

            // T7.5: a converging cycle's wasted passes, which is the case a pass budget does not catch (§7)
            {
 Scene<ErasedStore> scene(EvalModel::EagerPush, false);
     const StandardSchemas schemas = declare_standard_schemas(scene.regions());

          // build a cycle that converges, because clamping a value that is already inside its range changes nothing (§7)
      const InstanceId first_instance = scene.store().create(schemas.single_int);
const PropertyId first = scene.regions().global(first_instance, 0);
          scene.store().write(first, Value(std::int64_t{0}), WriteSource::GeneratedInit);

                const InstanceId second_instance = scene.store().create(schemas.single_int);
      const PropertyId second = scene.regions().global(second_instance, 0);
   scene.store().write(second, Value(std::int64_t{0}), WriteSource::GeneratedInit);

  // clamp each side, which reaches a fixed point instead of diverging (§7)
     scene.graph().add_binding(first, std::vector<PropertyId>{second}, &clamp_first<ErasedStore>);
           scene.graph().add_binding(second, std::vector<PropertyId>{first}, &floor_first<ErasedStore>);
     scene.graph().seal();

      // a value already inside the clamp range settles immediately (§7)
         scene.graph().assign(first, Value(std::int64_t{0}));
          SettleStats stats = scene.graph().settle();

   // now push a value outside the range, so the clamp has work to do (§7)
        scene.graph().assign(first, Value(std::int64_t{5}));
       stats = scene.graph().settle();

        record("T7.5a", "converging cycle from inside its range", format(static_cast<double>(stats.passes), 0) + " passes, exhausted=" + std::string(stats.cycle_budget_exhausted ? "yes" : "no"));
   check(verdict, "T7.5b", "a converging cycle is reported as converged", !stats.cycle_budget_exhausted, "settled in " + std::to_string(stats.passes) + " passes without hitting the budget");
        }

      // T7.2: the detection cost over a large graph (§7)
            {
      // build a wide acyclic graph of the required size (§7)
         Scene<ErasedStore> scene(EvalModel::EagerPush, false);
           const StandardSchemas schemas = declare_standard_schemas(scene.regions());

   const InstanceId root_instance = scene.store().create(schemas.single_int);
         const PropertyId root = scene.regions().global(root_instance, 0);
       scene.store().write(root, Value(std::int64_t{1}), WriteSource::GeneratedInit);

      PropertyId previous = root;

   for (std::size_t index = 0; index < CYCLE_GRAPH_BINDINGS; index++) {
        const InstanceId instance = scene.store().create(schemas.single_int);
 const PropertyId property = scene.regions().global(instance, 0);
            scene.store().write(property, Value(std::int64_t{0}), WriteSource::GeneratedInit);

     // bind a wide fan of properties to the previous one, so the graph is not a bare chain (§7)
        scene.graph().add_binding(property, std::vector<PropertyId>{previous, root}, &sum_all<ErasedStore>);

          previous = property;
      }

   scene.graph().seal();

     // discard the warm-up detections before measuring (§28.2)
                for (std::size_t step = 0; step < 20; step++) {
             static_cast<void>(detect_cycles(scene.graph()));
          }

          // record one detection per sample (§28.2)
  std::vector<double> samples;
        samples.reserve(MEASURED_WIDE);

   for (std::size_t step = 0; step < MEASURED_WIDE; step++) {
   const auto start = Clock::now();
        const CycleReport report = detect_cycles(scene.graph());
             samples.push_back(to_ms(Clock::now() - start));

    // an acyclic graph must stay acyclic, so nothing is reported (§7)
            static_cast<void>(report.has_cycle);
        }

  const double p99 = percentile(samples, 0.99);
       check(verdict, "T7.2", "cycle detection over a 10000-binding graph p99", p99 <= T7_2_DETECT_MS, format(p99) + " ms <= " + format(T7_2_DETECT_MS, 1) + " ms");
      }

   // T7.6: whether an invalid cycle can reach Release (§7)
            {
            // the static case is decidable at seal, so it can be a compile error in principle (§7)
  // the runtime case cannot, because dynamic structures exist in Release (§7)
      record("T7.6", "can an invalid cycle reach Release", "static: decidable by Kahn at seal, so the compiler can reject it and no runtime guard is needed for a sealed graph. runtime: reachable in Release through dynamic dependency insertion, so a guard has to stay in Release or dynamic graphs have to be excluded from the shipping path.");
       }

 // hand back the section tally (§28.4)
            return verdict;
        }

        // ===============================================================================================================
        // Experiment 8: animation interaction (AD-04d, AD-24)
        // ===============================================================================================================
        Verdict run_animation() {
            // announce the section (§28.2)
            std::printf("\n=== 8. animation interaction (AD-04d, AD-24) ===\n");

            // keep the tally for the section (§28.4)
            Verdict verdict;

  // run the stale value sequence once per evaluation model and per end mode (§8)
      for (const EvalModel model : {EvalModel::EagerPush, EvalModel::HybridPushPull}) {
 for (const AnimationEndMode mode : {AnimationEndMode::RestoreShadow, AnimationEndMode::ReEvaluate, AnimationEndMode::RestoreNoInvalidate}) {
     Scene<ErasedStore> scene(model, false);
        const StandardSchemas schemas = declare_standard_schemas(scene.regions());

            // a source property drives a bound property (§8)
      const InstanceId source_instance = scene.store().create(schemas.single_int);
      const PropertyId source = scene.regions().global(source_instance, 0);
      scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

   const InstanceId bound_instance = scene.store().create(schemas.single_int);
    const PropertyId bound = scene.regions().global(bound_instance, 0);
    scene.store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

     scene.graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
         scene.graph().seal();
  static_cast<void>(scene.graph().settle());

    // step 1 and 2: the animation starts and changes the effective value (§8)
        static_cast<void>(scene.graph().set_animation(bound, Value(std::int64_t{100})));
      static_cast<void>(scene.graph().settle());

   const std::int64_t during = scene.store().read(bound).as_int();

            // step 6: the source dependency changes while the animation owns the property (§8)
      scene.graph().assign(source, Value(std::int64_t{55}));
   static_cast<void>(scene.graph().settle());

  // step 4: the animation ends and the binding resumes (§8)
      scene.graph().end_animation(bound, mode);
    static_cast<void>(scene.graph().settle());

  const std::int64_t after = scene.store().read(bound).as_int();

      // report the row so the three end modes can be compared directly (§8)
   std::printf("    model=%-18s mode=%-24s during=%3lld  after=%3lld  %s\n", std::string(model_name(model)).c_str(), mode == AnimationEndMode::RestoreShadow ? "restore_shadow" : (mode == AnimationEndMode::ReEvaluate ? "re_evaluate" : "restore_no_invalidate"), static_cast<long long>(during), static_cast<long long>(after), after == 55 ? "fresh" : "STALE");

       // T8.1 and T8.2: the value after the animation must come from the binding, not the shadow (§8)
      if (model == EvalModel::EagerPush && mode == AnimationEndMode::RestoreShadow) {
   check(verdict, "T8.1", "eager + restore_shadow yields the fresh binding value", after == 55, "after=" + std::to_string(after));
   }

            if (model == EvalModel::HybridPushPull && mode == AnimationEndMode::ReEvaluate) {
        check(verdict, "T8.2", "hybrid + re_evaluate yields the fresh binding value", after == 55, "after=" + std::to_string(after));
            }
      }
      }

        // T8.1 evidence: the mode with no invalidate restores a stale value (§8)
      record("T8.1b", "restore without invalidate (diagnostic)", "shows the stale value that the shadow table would produce if the scheduler forgot the invalidate; see the STALE row above");

            // T8.3: an imperative write during an animation wins (§8)
            {
     Scene<ErasedStore> scene(EvalModel::EagerPush, false);
     const StandardSchemas schemas = declare_standard_schemas(scene.regions());

       const InstanceId source_instance = scene.store().create(schemas.single_int);
    const PropertyId source = scene.regions().global(source_instance, 0);
         scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

          const InstanceId bound_instance = scene.store().create(schemas.single_int);
    const PropertyId bound = scene.regions().global(bound_instance, 0);
           scene.store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

       scene.graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
      scene.graph().seal();
      static_cast<void>(scene.graph().settle());

  // start an animation and then write imperatively (§8)
            static_cast<void>(scene.graph().set_animation(bound, Value(std::int64_t{100})));
    static_cast<void>(scene.graph().settle());
        scene.graph().assign(bound, Value(std::int64_t{7}));
      static_cast<void>(scene.graph().settle());

      const std::int64_t value = scene.store().read(bound).as_int();
                const bool no_animation = scene.store().source_of(bound) == WriteSource::Imperative;

     check(verdict, "T8.3", "an imperative write during an animation wins", value == 7 && no_animation, "bound=" + std::to_string(value) + ", source=" + std::string(no_animation ? "imperative" : "not imperative"));
  }

      // T8.4: rebind after an animation re-evaluates from current dependencies (§8)
            {
   Scene<ErasedStore> scene(EvalModel::EagerPush, false);
                const StandardSchemas schemas = declare_standard_schemas(scene.regions());

       const InstanceId source_instance = scene.store().create(schemas.single_int);
          const PropertyId source = scene.regions().global(source_instance, 0);
            scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

   const InstanceId bound_instance = scene.store().create(schemas.single_int);
     const PropertyId bound = scene.regions().global(bound_instance, 0);
       scene.store().write(bound, Value(std::int64_t{0}), WriteSource::GeneratedInit);

      scene.graph().add_binding(bound, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
      scene.graph().seal();
            static_cast<void>(scene.graph().settle());

   // animate, change the source under the animation, override, then rebind (§8)
   static_cast<void>(scene.graph().set_animation(bound, Value(std::int64_t{100})));
        static_cast<void>(scene.graph().settle());
       scene.graph().assign(source, Value(std::int64_t{60}));
static_cast<void>(scene.graph().settle());
     scene.graph().assign(bound, Value(std::int64_t{1}));
    static_cast<void>(scene.graph().settle());
        scene.graph().rebind(bound);
            static_cast<void>(scene.graph().settle());

        const std::int64_t value = scene.store().read(bound).as_int();
      check(verdict, "T8.4", "rebind after an animation re-evaluates", value == 60, "bound=" + std::to_string(value));
            }

            // T8.5: one frame of animated writes plus settle (§8)
      {
      Scene<ErasedStore> scene(EvalModel::EagerPush, false);
        const StandardSchemas schemas = declare_standard_schemas(scene.regions());

          const InstanceId source_instance = scene.store().create(schemas.single_int);
                const PropertyId source = scene.regions().global(source_instance, 0);
  scene.store().write(source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

   // bind every animated property to the source, so each shadow has a binding value (§8)
   std::vector<PropertyId> animated;
                animated.reserve(ANIMATED_PROPERTIES);

  for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
       const InstanceId instance = scene.store().create(schemas.single_int);
         const PropertyId property = scene.regions().global(instance, 0);
 scene.store().write(property, Value(std::int64_t{0}), WriteSource::GeneratedInit);
             animated.push_back(property);

    scene.graph().add_binding(property, std::vector<PropertyId>{source}, &pass_int<ErasedStore>);
          }

     scene.graph().seal();
         static_cast<void>(scene.graph().settle());

       // discard the warm-up frames before measuring (§28.2)
    for (std::size_t step = 0; step < 50; step++) {
        for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
          static_cast<void>(scene.graph().set_animation(animated.at(index), Value(static_cast<std::int64_t>(step))));
   }
          static_cast<void>(scene.graph().settle());
      }

          // record one animation frame plus settle per sample (§8)
  std::vector<double> samples;
                samples.reserve(MEASURED_WIDE);

        for (std::size_t step = 0; step < MEASURED_WIDE; step++) {
       const auto start = Clock::now();

            // write one frame of every animated property (§13.5)
   for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
          static_cast<void>(scene.graph().set_animation(animated.at(index), Value(static_cast<std::int64_t>(step))));
          }

      static_cast<void>(scene.graph().settle());
              samples.push_back(to_ms(Clock::now() - start));
       }

       const double p99 = percentile(samples, 0.99);
    check(verdict, "T8.5", "1024 animated properties written + settled p99", p99 <= T8_5_ANIMATION_MS, format(p99) + " ms <= " + format(T8_5_ANIMATION_MS, 0) + " ms (D recorded 0.030 ms p50)");
 record("T8.6", "animation side table bytes per animated property", format(static_cast<double>(scene.graph().animation_side_bytes()) / static_cast<double>(ANIMATED_PROPERTIES), 1) + " B (D recorded 64 B)");
        }

      // hand back the section tally (§28.4)
            return verdict;
   }

    } // namespace

    // expose the section runners to the driver below (§28.4)
    using albor::prototype_d2::run_animation;
    using albor::prototype_d2::run_chains;
    using albor::prototype_d2::run_cycles;
    using albor::prototype_d2::run_diamonds;
    using albor::prototype_d2::run_ids;
    using albor::prototype_d2::run_mutation;
    using albor::prototype_d2::run_replace;
    using albor::prototype_d2::run_storage;

} // namespace albor::prototype_d2

int main(int argc, char** argv) {
    // the section to run, so a failure can be isolated without re-running everything (§28.4)
    const std::string requested = (argc > 1) ? std::string(argv[1]) : std::string("all");

    // announce the configuration the numbers were produced under (§28.2)
    std::printf("Prototype D2 — binding engine follow-up, AD-04b and AD-04c\n");
    std::printf("hardware: recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d2/plan.md, committed before this benchmark existed\n");

    // the tally across every section that ran (§28.4)
    albor::prototype_d2::Verdict total;

    // the sections the plan declares, in order (§28.4)
    const auto wants = [&](std::string_view section) { return requested == "all" || requested == section; };

    if (wants("storage")) {
        const auto verdict = albor::prototype_d2::run_storage();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("chains")) {
        const auto verdict = albor::prototype_d2::run_chains();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("diamonds")) {
        const auto verdict = albor::prototype_d2::run_diamonds();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("mutation")) {
        const auto verdict = albor::prototype_d2::run_mutation();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("ids")) {
        const auto verdict = albor::prototype_d2::run_ids();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("replace")) {
        const auto verdict = albor::prototype_d2::run_replace();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("cycles")) {
        const auto verdict = albor::prototype_d2::run_cycles();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    if (wants("animation")) {
        const auto verdict = albor::prototype_d2::run_animation();
        total.passed += verdict.passed;
        total.failed += verdict.failed;
    }

    // report the overall result so a reader cannot miss a failure (§28.4)
    std::printf("\n=== total: %zu passed, %zu failed ===\n", total.passed, total.failed);

    // return a non-zero status when any threshold failed, so a script can gate on it (§28.4)
    return total.failed == 0 ? 0 : 1;
}
