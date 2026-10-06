#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "prototypes/d/engine.h"
#include "prototypes/d/strategies.h"
#include "prototypes/d/strategy_dirty_bitset.h"
#include "prototypes/d/workload.h"

namespace albor::prototype_d
{

    namespace
    {

        // fix the workload and its thresholds before the run (§28.4)
        constexpr WorkloadShape SHAPE;

        // discard this many updates before measuring
        constexpr std::size_t WARMUP_UPDATES = 200;

        // record this many samples for the narrow and the snapshot update
        constexpr std::size_t MEASURED_UPDATES = 2000;

        // record this many samples for the wide update
        constexpr std::size_t WIDE_UPDATES = 50;

        // record this many animation frames
        constexpr std::size_t ANIMATION_FRAMES = 2000;

        // allow at most one frame for a narrow update at p99
        constexpr double SETTLE_LATENCY_BUDGET_MS = 16.0;

        // allow at most one frame for an animation update at p99
        constexpr double ANIMATION_LATENCY_BUDGET_MS = 16.0;

        // allow at most one frame for a wide update at p99
        constexpr double WIDE_LATENCY_BUDGET_MS = 16.0;

        using Clock = std::chrono::steady_clock;

        double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

        double percentile(std::vector<double> samples, double fraction) {
            // report no latency when nothing was measured
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index
            std::ranges::sort(samples);

            // clamp the index so a fraction of one stays inside the samples
            const std::size_t index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));

            // hand back the sample at that position
            return samples.at(index);
        }

        std::unique_ptr<InvalidationStrategy> make_compact_records() { return std::make_unique<CompactRecordsStrategy>(); }
        std::unique_ptr<InvalidationStrategy> make_dirty_bitset() { return std::make_unique<DirtyBitsetStrategy>(); }
        std::unique_ptr<InvalidationStrategy> make_shared_nodes() { return std::make_unique<SharedNodesStrategy>(); }

        // write one node input and settle to time the whole update
        double timed_input_update(WorkloadScene& scene, std::size_t node, std::uint64_t step, EvaluationStats* stats_out) {
            // start the clock before the write so the whole update is timed
            const Clock::time_point start = Clock::now();

            // write the input property the node reads
            scene.engine->assign(scene.handles.inputs.at(node % scene.handles.inputs.size()), Value(static_cast<double>(step)));

            // settle the scene on the UI thread
            const EvaluationStats stats = scene.engine->evaluate();

            // hand the work report to the caller when it asked for one
            if (stats_out != nullptr) {
                *stats_out = stats;
            }

            // report the latency of the whole update
            return to_ms(Clock::now() - start);
        }

        // write the shared environment property to invalidate a wide part of the scene
        double timed_wide_update(WorkloadScene& scene, std::uint64_t step, EvaluationStats* stats_out) {
            // start the clock before the write so the whole update is timed
            const Clock::time_point start = Clock::now();

            // write the environment property that every binding reads (§13.1)
            scene.engine->assign(scene.handles.theme, Value(static_cast<std::int64_t>(step)));

            // settle the scene on the UI thread
            const EvaluationStats stats = scene.engine->evaluate();

            // hand the work report to the caller when it asked for one
            if (stats_out != nullptr) {
                *stats_out = stats;
            }

            // report the latency of the whole update
            return to_ms(Clock::now() - start);
        }

        // publish immutable data from a worker and settle on the UI thread (§10.5)
        double timed_snapshot_update(WorkloadScene& scene, std::uint64_t step, EvaluationStats* stats_out) {
            // start the clock before the publish so the whole update is timed
            const Clock::time_point start = Clock::now();

            // publish the immutable payload from a worker thread (§10.5)
            std::thread worker([&scene, step] { publish_snapshot(scene, make_snapshot(1024 + step)); });

            // wait for the worker to queue its write
            worker.join();

            // settle the scene on the UI thread
            const EvaluationStats stats = scene.engine->evaluate();

            // hand the work report to the caller when it asked for one
            if (stats_out != nullptr) {
                *stats_out = stats;
            }

            // report the latency of the whole update
            return to_ms(Clock::now() - start);
        }

        // write one animation frame and settle, as the frame scheduler would drive it (§13.5)
        double timed_animation_frame(WorkloadScene& scene, std::uint64_t frame, EvaluationStats* stats_out) {
            // start the clock before the frame writes
            const Clock::time_point start = Clock::now();

            // publish one frame of every animated property (§13.5)
            static_cast<void>(advance_animation(scene, frame));

            // settle the scene so the readers of the animated values catch up
            const EvaluationStats stats = scene.engine->evaluate();

            // hand the work report to the caller when it asked for one
            if (stats_out != nullptr) {
                *stats_out = stats;
            }

            // report the latency of the whole animation frame
            return to_ms(Clock::now() - start);
        }

        // own the scene so that each candidate is built, measured and destroyed in turn
        void report(std::string_view name, WorkloadScene scene) {
            // read the memory report of the candidate before measuring it
            const EngineMemory usage = scene.engine->memory();

            // collect the latency samples of the narrow update
            std::vector<double> input_latencies;

            // collect the latency samples of the wide update
            std::vector<double> wide_latencies;

            // collect the latency samples of the worker snapshot update
            std::vector<double> snapshot_latencies;

            // collect the latency samples of the animation frames
            std::vector<double> animation_latencies;

            // collect the work report of the narrow update
            EvaluationStats input_stats;

            // collect the work report of the wide update
            EvaluationStats wide_stats;

            // collect the work report of the worker snapshot update
            EvaluationStats snapshot_stats;

            // collect the work report of the last animation frame
            EvaluationStats animation_stats;

            // reserve room so that the measurement does not allocate
            input_latencies.reserve(MEASURED_UPDATES);
            wide_latencies.reserve(WIDE_UPDATES);
            snapshot_latencies.reserve(MEASURED_UPDATES);

            // warm the caches and the pages before measuring
            for (std::size_t index = 0; index < WARMUP_UPDATES; index++) {
                static_cast<void>(timed_input_update(scene, index, index, nullptr));
            }

            // measure the narrow update that a single input write causes
            for (std::size_t index = 0; index < MEASURED_UPDATES; index++) {
                input_latencies.push_back(timed_input_update(scene, index * 7, index, &input_stats));
            }

            // measure the wide update that an environment write causes
            for (std::size_t index = 0; index < WIDE_UPDATES; index++) {
                wide_latencies.push_back(timed_wide_update(scene, index, &wide_stats));
            }

            // measure the update that a worker published snapshot causes
            for (std::size_t index = 0; index < MEASURED_UPDATES; index++) {
                snapshot_latencies.push_back(timed_snapshot_update(scene, index, &snapshot_stats));
            }

            // start the animation frame measurement
            scene.engine->reset_invalidated_total();

            // measure the frames the animation layer drives (§13.5)
            for (std::size_t index = 0; index < ANIMATION_FRAMES; index++) {
                animation_latencies.push_back(timed_animation_frame(scene, index, &animation_stats));
            }

            // read the invalidation count of the measured window (§28.2)
            const std::size_t invalidated_in_window = scene.engine->invalidated_total();

            // read the memory again now that the animation layer is shadowing binding values (§13.5)
            const EngineMemory animated_usage = scene.engine->memory();

            // divide the property storage by the property count of the fixed shape
            const double per_property = static_cast<double>(usage.property_slots) / static_cast<double>(SHAPE.property_count);

            // divide the binding records and their dependencies by the binding count
            const double per_binding = static_cast<double>(usage.binding_records + usage.declared_dependencies) / static_cast<double>(SHAPE.binding_count);

            // divide the index by the binding count of the fixed shape
            const double index_per_binding = static_cast<double>(usage.dependency_index) / static_cast<double>(SHAPE.binding_count);

            // count the candidate visits that planned work rejected as redundant (§28.2)
            const std::size_t redundant_visits = input_stats.candidates_considered > input_stats.bindings_planned ? input_stats.candidates_considered - input_stats.bindings_planned : 0;

            // measure the invalidation throughput over the animation window (§28.2)
            const double invalidations_per_frame = static_cast<double>(invalidated_in_window) / static_cast<double>(ANIMATION_FRAMES);

            std::printf("\n== %s ==\n", std::string(name).c_str());
            std::printf("  properties              %zu\n", SHAPE.property_count);
            std::printf("  bindings declared       %zu\n", scene.engine->binding_count());
            std::printf("  bytes per property      %.2f\n", per_property);
            std::printf("  bytes per binding       %.2f (record plus declared dependencies)\n", per_binding);
            std::printf("  index bytes per binding %.2f\n", index_per_binding);
            std::printf("  index bytes total       %zu\n", usage.dependency_index);
            std::printf("  animation side bytes    %zu for %zu animated properties\n", animated_usage.animation_side_table, scene.store->animated_count());
            std::printf("  input update ms p50     %.4f\n", percentile(input_latencies, 0.50));
            std::printf("  input update ms p95     %.4f\n", percentile(input_latencies, 0.95));
            std::printf("  input update ms p99     %.4f\n", percentile(input_latencies, 0.99));
            std::printf("  wide update ms p50      %.4f\n", percentile(wide_latencies, 0.50));
            std::printf("  wide update ms p99      %.4f\n", percentile(wide_latencies, 0.99));
            std::printf("  snapshot update ms p50  %.4f\n", percentile(snapshot_latencies, 0.50));
            std::printf("  snapshot update ms p99  %.4f\n", percentile(snapshot_latencies, 0.99));
            std::printf("  animation frame ms p50 %.4f\n", percentile(animation_latencies, 0.50));
            std::printf("  animation frame ms p99 %.4f\n", percentile(animation_latencies, 0.99));
            std::printf("  invalidations per frame %.1f\n", invalidations_per_frame);
            std::printf("  redundant candidate visits %zu\n", redundant_visits);
            std::printf("  bindings planned per update %.1f\n", static_cast<double>(input_stats.bindings_planned));
            std::printf("  input update work       %zu bindings in %zu passes, %zu candidates considered\n", input_stats.bindings_evaluated, input_stats.passes, input_stats.candidates_considered);
            std::printf("  wide update work        %zu bindings in %zu passes, %zu candidates considered\n", wide_stats.bindings_evaluated, wide_stats.passes, wide_stats.candidates_considered);
            std::printf("  snapshot update work    %zu bindings in %zu passes, %zu candidates considered\n", snapshot_stats.bindings_evaluated, snapshot_stats.passes, snapshot_stats.candidates_considered);
            std::printf("  cycle budget exhausted  %s\n", input_stats.cycle_budget_exhausted ? "yes" : "no");

            // compare the p99 of the measured categories with the recorded thresholds (§28.4)
            const bool within_budget = percentile(input_latencies, 0.99) <= SETTLE_LATENCY_BUDGET_MS && percentile(wide_latencies, 0.99) <= WIDE_LATENCY_BUDGET_MS;

            // report whether the candidate met the thresholds recorded before the run

            std::printf("  within frame budget     %s (p99 budget %.1f ms per category)\n", within_budget ? "yes" : "no", SETTLE_LATENCY_BUDGET_MS);

            // compare the animation p99 with the frame budget (§28.4)
            const bool animation_within_budget = percentile(animation_latencies, 0.99) <= ANIMATION_LATENCY_BUDGET_MS;

            // report whether an animation frame met the threshold recorded before the run
            std::printf("  animation within budget %s (p99 budget %.1f ms)\n", animation_within_budget ? "yes" : "no", ANIMATION_LATENCY_BUDGET_MS);
        }

    } // namespace

    void run_benchmark() {
        // name the prototype and the decision it feeds
        std::printf("Albor Prototype D: property and binding engine (§30, AD-04)\n");

        // state the fixed shape so the numbers can be reproduced (§28.4)
        std::printf("shape: %zu properties, target %zu bindings, chain depth %zu\n", SHAPE.property_count, SHAPE.binding_count, SHAPE.chain_depth);

        // state the thresholds that were recorded before the run
        std::printf("recorded budget: input and wide update p99 at most %.1f ms\n", SETTLE_LATENCY_BUDGET_MS);

        // measure the compact dependency records
        report("compact_records", build_scene(SHAPE, make_compact_records()));

        // measure the dirty bitset with topological ordering
        report("dirty_bitset_topo", build_scene(SHAPE, make_dirty_bitset()));

        // measure the shared dependency nodes
        report("shared_dep_nodes", build_scene(SHAPE, make_shared_nodes()));
    }

} // namespace albor::prototype_d

int main() {
    // run the recorded workload and print its measurements
    albor::prototype_d::run_benchmark();

    // report a successful run to the shell
    return 0;
}
