// Prototype D3 R3 — geometric slab growth, and D2's two failed timing thresholds as labelled variants
//
// plan.md was committed before this program existed (§28.4). The thresholds below are transcribed from it.
// Measurement code; not a runtime module (AGENTS.md, §32).

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        // T3.3.2 the crossover must sit at or below this many instances (512 properties)
        constexpr std::size_t T3_3_2_CROSSOVER_INSTANCES = 64;

        // T3.3.3 slack as a share of allocated slab bytes, at 4096 instances
        constexpr double T3_3_3_SLACK_SHARE = 0.25;

        // T3.3.6 the settle variant must meet D2's original T1.6 budget, in nanoseconds
        constexpr double T3_3_6_SETTLE_P99_NS = 250.0;

        // T3.3.7 the payload variant must meet D2's original T1.5 budget, in nanoseconds
        constexpr double T3_3_7_PAYLOAD_NS = 200.0;

        // the instance counts the growth sweep reports (§28.2)
        constexpr std::size_t SWEEP_COUNTS[] = {1, 16, 32, 64, 128, 256, 512, 1024, 4096, 8192};

        // the timing window: enough samples for a p99 to mean something, repeated per run (§28.2)
        constexpr std::size_t TIMING_SAMPLES = 5000;

        // the number of independent runs each timing threshold is judged over (§28.2)
        constexpr std::size_t TIMING_RUNS = 5;

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        std::string format(double value, int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // D2's realistic mix, one of every §13.1 category (§28.2)
        const std::vector<albor::prototype_d2::ValueType>& mixed_types() {
            static const std::vector<albor::prototype_d2::ValueType> types{
                albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double, albor::prototype_d2::ValueType::Bool, albor::prototype_d2::ValueType::String, albor::prototype_d2::ValueType::TranslatedString, albor::prototype_d2::ValueType::Snapshot, albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double,
            };
            return types;
        }

        // the total bytes one typed store holds, in the four columns R1 defined (§28.2)
        struct Totals
        {
            double value = 0.0;
            double addressability = 0.0;
            double slack = 0.0;
            double region_overhead = 0.0;
            double total = 0.0;
            double properties = 0.0;
        };

        Totals measure_typed(albor::prototype_d2::RegionTable& regions, albor::prototype_d2::TypedStore& store, albor::prototype_d2::SchemaId schema, const std::size_t instances) {
            // the region table resolves the schemas and must outlive the store (§5)
            static_cast<void>(regions);

            // create the live instances the measurement is over (§5)
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }

            Totals row;
            row.properties = static_cast<double>(store.property_count());
            row.value = static_cast<double>(store.value_bytes());
            row.addressability = static_cast<double>(store.address_bytes());
            row.slack = static_cast<double>(store.slab_allocated_bytes()) - row.value;
            row.region_overhead = static_cast<double>(store.region_table_bytes() + store.region_base_bytes());
            row.total = row.value + row.addressability + row.slack + row.region_overhead;
            return row;
        }

        Totals measure_erased(albor::prototype_d2::RegionTable& regions, albor::prototype_d2::ErasedStore& store, albor::prototype_d2::SchemaId schema, const std::size_t instances) {
            // create the live instances the measurement is over (§5)
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }

            Totals row;
            row.properties = static_cast<double>(store.property_count());
            row.value = static_cast<double>(store.memory_bytes());
            row.addressability = 0.0;
            row.slack = static_cast<double>(store.slot_capacity_bytes()) - row.value;
            row.region_overhead = static_cast<double>(regions.region_table_bytes());
            row.total = row.value + row.addressability + row.slack + row.region_overhead;
            return row;
        }

        Totals per_property(Totals row) {
            // a store with no properties cannot be normalised (§28.2)
            if (row.properties <= 0.0) {
                return row;
            }

            row.value /= row.properties;
            row.addressability /= row.properties;
            row.slack /= row.properties;
            row.region_overhead /= row.properties;
            row.total /= row.properties;
            return row;
        }

        // the p-level of a sorted sample vector, guarded against an empty window (§28.2)
        double level(std::vector<double> samples, const double fraction) {
            if (samples.empty()) {
                return 0.0;
            }

            std::sort(samples.begin(), samples.end());
            const auto index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));
            return samples.at(index);
        }

        double median(std::vector<double> samples) {
            if (samples.empty()) {
                return 0.0;
            }

            std::sort(samples.begin(), samples.end());
            const std::size_t middle = samples.size() / 2;
            if (samples.size() % 2 == 1) {
                return samples.at(middle);
            }
            return (samples.at(middle - 1) + samples.at(middle)) / 2.0;
        }

        double nanoseconds(const std::chrono::steady_clock::duration delta) { return std::chrono::duration<double, std::nano>(delta).count(); }

        // one binding that adds one to its source, the smallest settle a graph can run (§28.2)
        template <class Store>
        void one_plus_binding(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) + 1);
        }

        // the single write + settle timing, run once and sampled per iteration (§28.2)
        std::vector<double> run_settle_window(albor::prototype_d2::Graph<albor::prototype_d2::TypedStore>& graph, const albor::prototype_d2::PropertyId source, const std::size_t samples) {
            std::vector<double> durations;
            durations.reserve(samples);

            std::int64_t value = 0;
            for (std::size_t index = 0; index < samples; index++) {
                // write a new value, which the binding recomputes on the settle (§13.3)
                const auto begin = std::chrono::steady_clock::now();
                graph.assign(source, albor::prototype_d2::Value(++value));
                static_cast<void>(graph.settle());
                const auto end = std::chrono::steady_clock::now();

                durations.push_back(nanoseconds(end - begin));
            }

            return durations;
        }

    } // namespace

    void run_slab_growth(Verdict& verdict) {
        std::printf("--- part A: geometric slab growth, mixed schema ---\n");
        std::printf("  %-10s %-9s %-12s %-12s %-10s\n", "instances", "props", "typed B/p", "erased B/p", "erased/typed");

        // remember the one-instance rows for T3.3.1 (§28.4)
        Totals typed_at_one;
        Totals erased_at_one;

        // remember the largest typed row so T3.3.3 is judged where the slab is under pressure (§28.4)
        Totals typed_largest;

        // remember the first instance count at which typed stops losing (§28.2)
        std::size_t crossover_instances = 0;

        for (const std::size_t instances : SWEEP_COUNTS) {
            // the typed candidate under the geometric policy (plan.md R3)
            albor::prototype_d2::RegionTable typed_regions;
            const albor::prototype_d2::SchemaId typed_schema = typed_regions.add_schema("Typed", albor::prototype_d2::InstanceKind::Component, mixed_types());
            albor::prototype_d2::TypedStore typed(typed_regions);
            typed.set_slab_growth(albor::prototype_d2::TypedStore::SlabGrowth::Geometric);
            const Totals typed_row = measure_typed(typed_regions, typed, typed_schema, instances);

            // the erased candidate over the same declared types (§28.4)
            albor::prototype_d2::RegionTable erased_regions;
            const albor::prototype_d2::SchemaId erased_schema = erased_regions.add_schema("Erased", albor::prototype_d2::InstanceKind::Component, mixed_types());
            albor::prototype_d2::ErasedStore erased(erased_regions);
            const Totals erased_row = measure_erased(erased_regions, erased, erased_schema, instances);

            if (instances == 1) {
                typed_at_one = typed_row;
                erased_at_one = erased_row;
            }

            if (instances == SWEEP_COUNTS[std::size(SWEEP_COUNTS) - 2]) {
                typed_largest = typed_row;
            }

            if (crossover_instances == 0 && typed_row.total <= erased_row.total) {
                crossover_instances = instances;
            }

            std::printf("  %-10zu %-9.0f %-12s %-12s %-10s\n", instances, typed_row.properties, format(per_property(typed_row).total, 1).c_str(), format(per_property(erased_row).total, 1).c_str(), format(typed_row.total > 0.0 ? erased_row.total / typed_row.total : 0.0).c_str());
        }

        // T3.3.1: the one-instance blowup R1 measured must be gone, not merely smaller (§28.4)
        check(verdict, "T3.3.1", "typed total <= erased total at 1 instance", typed_at_one.total <= erased_at_one.total, format(typed_at_one.total, 0) + " B <= " + format(erased_at_one.total, 0) + " B");

        // T3.3.2: the crossover must sit at or below 64 instances (§28.4)
        check(verdict, "T3.3.2", "crossover <= 64 instances (512 properties)", crossover_instances != 0 && crossover_instances <= T3_3_2_CROSSOVER_INSTANCES, crossover_instances == 0 ? std::string("not reached up to 8192 instances") : std::to_string(crossover_instances) + " instances <= " + std::to_string(T3_3_2_CROSSOVER_INSTANCES));

        // T3.3.3: the amortisation at scale must survive the policy change (§28.4)
        const double allocated = typed_largest.value + typed_largest.slack;
        const double share = allocated > 0.0 ? typed_largest.slack / allocated : 0.0;
        check(verdict, "T3.3.3", "slack / allocated at 4096 instances", share <= T3_3_3_SLACK_SHARE, format(share * 100.0, 1) + "% <= " + format(T3_3_3_SLACK_SHARE * 100.0, 0) + "% (" + format(typed_largest.slack, 0) + " B slack of " + format(allocated, 0) + " B)");

        // T3.3.5: the ratio is the finding, not a threshold (§28.4)
        record("T3.3.5", "erased / typed total at 1 instance", typed_at_one.total > 0.0 ? format(erased_at_one.total / typed_at_one.total) + "x" : "n/a");
    }

    void run_settle_variants(Verdict& verdict) {
        std::printf("\n--- part B: labelled timing variants, D2's original budgets ---\n");

        // the graph every variant runs: one binding, eager push, sealed CSR (§28.2)
        albor::prototype_d2::RegionTable regions;
        const std::vector<albor::prototype_d2::ValueType> schema_types{albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Int};
        const albor::prototype_d2::SchemaId schema = regions.add_schema("Counter", albor::prototype_d2::InstanceKind::Component, schema_types);
        albor::prototype_d2::TypedStore store(regions);
        const albor::prototype_d2::InstanceId instance = store.create(schema);
        const albor::prototype_d2::PropertyId source = regions.global(instance, static_cast<albor::prototype_d2::LocalPropertyId>(0));
        const albor::prototype_d2::PropertyId target = regions.global(instance, static_cast<albor::prototype_d2::LocalPropertyId>(1));

        albor::prototype_d2::Graph<albor::prototype_d2::TypedStore> graph(regions, store, albor::prototype_d2::EvalModel::EagerPush, false);
        const std::vector<albor::prototype_d2::PropertyId> dependencies{source};
        static_cast<void>(graph.add_binding(target, dependencies, &one_plus_binding<albor::prototype_d2::TypedStore>));
        graph.seal();

        // judge each variant over five independent runs and report median and p99 (§28.2)
        for (const bool reuse : {false, true}) {
            graph.set_reuse_settle_buffers(reuse);
            std::printf("  variant settle-plan-reuse=%s:\n", reuse ? "on" : "off (D2's recorded behaviour)");

            std::vector<double> run_p99;
            std::vector<double> run_p50;

            for (std::size_t run = 0; run < TIMING_RUNS; run++) {
                const std::vector<double> samples = run_settle_window(graph, source, TIMING_SAMPLES);
                run_p99.push_back(level(samples, 0.99));
                run_p50.push_back(level(samples, 0.50));
            }

            std::printf("    p50 median %-8s  p99 median %-8s  p99 spread %s..%s ns\n", format(median(run_p50), 1).c_str(), format(median(run_p99), 1).c_str(), format(*std::min_element(run_p99.begin(), run_p99.end()), 1).c_str(), format(*std::max_element(run_p99.begin(), run_p99.end()), 1).c_str());

            if (reuse) {
                // T3.3.6: the variant must meet D2's original T1.6 budget (§28.4)
                check(verdict, "T3.3.6", "settle-plan-reuse: write+settle p99 <= 250 ns", median(run_p99) <= T3_3_6_SETTLE_P99_NS, format(median(run_p99), 1) + " ns <= " + format(T3_3_6_SETTLE_P99_NS, 0) + " ns (D2 T1.6's original budget)");
            }
            else {
                // T3.3.8: the un-mitigated figure, recorded so the delta is visible (§28.4)
                record("T3.3.8", "un-mitigated write+settle p99", format(median(run_p99), 1) + " ns");
            }
        }
    }

    void run_payload_variants(Verdict& verdict) {
        std::printf("  variant shared-immutable-payload:\n");

        // one string payload and one snapshot payload, constructed once and aliased by every property (§13.1)
        const auto shared_text = std::make_shared<const std::string>("label text that every property aliases");
        const auto shared_samples = std::make_shared<const std::vector<double>>(std::vector<double>{1.0, 2.0, 3.0, 4.0});

        // the three non-trivial types of the mixed schema, round-robin (§28.2)
        const std::vector<albor::prototype_d2::ValueType> non_trivial{albor::prototype_d2::ValueType::String, albor::prototype_d2::ValueType::TranslatedString, albor::prototype_d2::ValueType::Snapshot};

        // measure the aliased construct + destroy pair, sampled per pair (§28.2)
        std::vector<double> run_p50;
        std::vector<double> run_p99;

        for (std::size_t run = 0; run < TIMING_RUNS; run++) {
            std::vector<double> samples;
            samples.reserve(TIMING_SAMPLES);

            std::size_t type_index = 0;
            for (std::size_t index = 0; index < TIMING_SAMPLES; index++) {
                const albor::prototype_d2::ValueType type = non_trivial.at(type_index++ % non_trivial.size());

                // construct one value aliasing the shared payload, then destroy it (§13.1)
                const auto begin = std::chrono::steady_clock::now();
                {
                    albor::prototype_d2::Value value;
                    switch (type) {
                    case albor::prototype_d2::ValueType::String:
                        value = albor::prototype_d2::Value(shared_text);
                        break;
                    case albor::prototype_d2::ValueType::TranslatedString:
                        value = albor::prototype_d2::Value(albor::prototype_d2::TranslatedString{7, shared_text});
                        break;
                    default:
                        value = albor::prototype_d2::Value(albor::prototype_d2::SnapshotBox{albor::prototype_d2::ValueType::Double, shared_samples, 4});
                        break;
                    }

                    // the value must survive to the destroy, or the pair is not measured (§28.4)
                    static_cast<void>(value.type());
                }
                const auto end = std::chrono::steady_clock::now();

                samples.push_back(nanoseconds(end - begin));
            }

            run_p50.push_back(level(samples, 0.50));
            run_p99.push_back(level(samples, 0.99));
        }

        std::printf("    p50 median %-8s  p99 median %-8s ns\n", format(median(run_p50), 1).c_str(), format(median(run_p99), 1).c_str());

        // T3.3.7: the variant must meet D2's original T1.5 budget (§28.4)
        check(verdict, "T3.3.7", "shared-immutable-payload: construct+destroy <= 200 ns", median(run_p99) <= T3_3_7_PAYLOAD_NS, format(median(run_p99), 1) + " ns p99 <= " + format(T3_3_7_PAYLOAD_NS, 0) + " ns (D2 T1.5's original budget)");

        // T3.3.8: the un-mitigated figure, which allocates a fresh payload per property (§28.4)
        std::vector<double> fresh_p50;
        std::vector<double> fresh_p99;

        for (std::size_t run = 0; run < TIMING_RUNS; run++) {
            std::vector<double> samples;
            samples.reserve(TIMING_SAMPLES);

            std::size_t type_index = 0;
            for (std::size_t index = 0; index < TIMING_SAMPLES; index++) {
                const albor::prototype_d2::ValueType type = non_trivial.at(type_index++ % non_trivial.size());

                // construct one value that owns a fresh payload, then destroy it (§13.1)
                const auto begin = std::chrono::steady_clock::now();
                {
                    albor::prototype_d2::Value value;
                    switch (type) {
                    case albor::prototype_d2::ValueType::String:
                        value = albor::prototype_d2::Value::make_string("a fresh payload every property allocates");
                        break;
                    case albor::prototype_d2::ValueType::TranslatedString:
                        value = albor::prototype_d2::Value::make_translated("a fresh payload every property allocates", 7);
                        break;
                    default:
                        value = albor::prototype_d2::Value::make_snapshot(std::make_shared<const std::vector<double>>(std::vector<double>{1.0, 2.0, 3.0, 4.0}));
                        break;
                    }

                    static_cast<void>(value.type());
                }
                const auto end = std::chrono::steady_clock::now();

                samples.push_back(nanoseconds(end - begin));
            }

            fresh_p50.push_back(level(samples, 0.50));
            fresh_p99.push_back(level(samples, 0.99));
        }

        record("T3.3.8", "un-mitigated payload construct+destroy p99", format(median(fresh_p99), 1) + " ns (shared variant: " + format(median(run_p99), 1) + " ns)");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R3 — geometric slab growth and labelled timing variants (AD-04b, D2 T1.5/T1.6)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R3, committed before this program existed\n");
    std::printf("part A is deterministic memory: single figures; part B is timing: %zu runs of %zu samples each\n\n", albor::prototype_d3::TIMING_RUNS, albor::prototype_d3::TIMING_SAMPLES);

    albor::prototype_d3::Verdict verdict;

    albor::prototype_d3::run_slab_growth(verdict);
    albor::prototype_d3::run_settle_variants(verdict);
    albor::prototype_d3::run_payload_variants(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
