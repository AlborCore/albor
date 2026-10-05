// Prototype D3 R10 — the read path, interleaved A/B with spread (F2.10)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// Prototype D2 measured the read path once: 43 ns for a global id, 44 ns through the base offset, 2.3 %
// apart, with no spread reported. This revision interleaves the variants so drift hits them equally, and
// decomposes what the 43 ns is made of, because a typed scalar read has no business costing 43 ns

#include "prototypes/d2/src/storage.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace albor::prototype_d3
{
    // report the machine's load, because a threshold about a 1 % claim is only meaningful on a quiet machine (§28.2)
    double load_average() {
        double samples[1] = {0.0};
        static_cast<void>(getloadavg(samples, 1));
        return samples[0];
    }

    namespace
    {

        // T3.10.1 without this bound an A/B on a 1 % claim decides nothing
        constexpr double T3_10_1_SPREAD = 0.10;

        // T3.10.2 a typed scalar read must stay far inside the frame's per-property budget
        constexpr double T3_10_2_TYPED_P99_NS = 20.0;

        // T3.10.2 and must not be materially dearer than the erased slot it replaces
        constexpr double T3_10_2_ERASED_RATIO = 1.5;

        // T3.10.3 D2's 2.3 %, restated with the spread bound D2 did not report
        constexpr double T3_10_3_INDIRECTION = 0.05;

        // T3.10.4 the scalar fast path against the 40 B variant copy
        constexpr double T3_10_4_FAST_PATH_RATIO = 2.0;

        // the committed measurement shape: 5 runs of 20 000 reads per path per run, in blocks of 2 000 (§28.4)
        constexpr std::size_t RUNS = 5;
        constexpr std::size_t READS_PER_RUN = 20000;
        constexpr std::size_t BLOCK = 2000;
        constexpr std::size_t WARMUP_READS = 2000;

        // the property space every path reads over, the same scale R2 accounted at (§28.2)
        constexpr std::size_t INSTANCES = 12500;

        // the accumulator every path folds its reads into, so no path can be optimised away (§28.2)
        std::uint64_t g_sink = 0;

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-8s %-58s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-8s %-58s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        std::string format(const double value, const int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // report the sample at a fraction, so a latency row is comparable across paths (§28.2)
        double percentile(std::vector<double> samples, const double fraction) {
            // report nothing when the path measured no sample (§28.2)
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

        using Clock = std::chrono::steady_clock;

        // report one block's elapsed time in nanoseconds (§28.2)
        double time_block_now() { return std::chrono::duration<double, std::nano>(Clock::now().time_since_epoch()).count(); }

        // one read target, carrying the dense id and the (instance, local) pair that resolves to it
        //
        // both addressing modes read this one element, so the global-id path and the base-offset path pay exactly
        // the same array traffic and differ only in how the address is formed (AD-04a) (§28.2)
        struct ReadTarget
        {
            albor::prototype_d2::PropertyId id = 0;
            albor::prototype_d2::InstanceId instance = 0;
            albor::prototype_d2::LocalPropertyId local = 0;
        };

        // hold the read order every path walks, so no path benefits from a friendlier access pattern (§28.2)
        using ReadOrder = std::vector<ReadTarget>;

        // build one shuffled order and the matching (instance, local) pairs, so the two addressing modes walk the
        // same memory traffic and differ only in how the address is formed (AD-04a) (§28.2)
        ReadOrder build_order(const albor::prototype_d2::RegionTable& regions) {
            ReadOrder order;

            // map every live property to the pair that resolves it (§28.2)
            for (std::size_t instance = 0; instance < regions.region_count(); instance++) {
                const albor::prototype_d2::InstanceId typed_instance = static_cast<albor::prototype_d2::InstanceId>(instance);
                const albor::prototype_d2::Region& region = regions.region(typed_instance);

                for (albor::prototype_d2::LocalPropertyId local = 0; local < region.property_count; local++) {
                    order.push_back(ReadTarget{region.first_id + local, typed_instance, local});
                }
            }

            // shuffle the order once, so every path reads the same sequence of properties (§28.2)
            std::vector<std::size_t> positions(order.size());

            for (std::size_t index = 0; index < positions.size(); index++) {
                positions.at(index) = index;
            }

            std::mt19937 generator(20260204u);
            std::ranges::shuffle(positions, generator);

            // materialise the shuffled order, carrying both addressing modes in one element (§28.2)
            ReadOrder shuffled;
            shuffled.reserve(positions.size());

            for (const std::size_t position : positions) {
                shuffled.push_back(order.at(position));
            }

            // hand back the order both paths walk (§28.2)
            return shuffled;
        }

        // hold one path's block timings and its per-run medians, so the spread can be reported (§28.2)
        struct PathSamples
        {
            std::string name;
            std::vector<double> samples;
            std::vector<double> run_p50;
        };

        // report a path's mean per-read cost over a block of reads, in nanoseconds (§28.2)
        double ns_per_read(const double block_ns) { return block_ns / static_cast<double>(BLOCK); }

        // report one path's run-to-run spread as a fraction of the mean, which T3.10.1 bounds (§28.2)
        double spread_of(const PathSamples& path) {
            // report nothing when the path recorded no run (§28.2)
            if (path.run_p50.empty()) {
                return 0.0;
            }

            double average = 0.0;

            for (const double median : path.run_p50) {
                average += ns_per_read(median);
            }

            average /= static_cast<double>(path.run_p50.size());

            // hand back the spread relative to the mean (§28.2)
            const auto [lowest, highest] = std::minmax_element(path.run_p50.begin(), path.run_p50.end());
            return average > 0.0 ? ns_per_read(*highest - *lowest) / average : 0.0;
        }

        // report a path's fastest run, which interference can only slow down and never speed up (§28.2)
        double min_of(const PathSamples& path) {
            // report nothing when the path recorded no run (§28.2)
            if (path.run_p50.empty()) {
                return 0.0;
            }

            // hand back the fastest run's per-read cost (§28.2)
            return ns_per_read(*std::ranges::min_element(path.run_p50.begin(), path.run_p50.end()));
        }

        // report one path's mean per-read cost across every run, in nanoseconds (§28.2)
        double mean_of(const PathSamples& path) {
            // report nothing when the path recorded no run (§28.2)
            if (path.run_p50.empty()) {
                return 0.0;
            }

            double average = 0.0;

            for (const double median : path.run_p50) {
                average += ns_per_read(median);
            }

            // hand back the mean of the run medians (§28.2)
            return average / static_cast<double>(path.run_p50.size());
        }

        // report one path's p99 per-read cost, in nanoseconds (§28.2)
        double p99_of(const PathSamples& path) { return ns_per_read(percentile(path.samples, 0.99)); }

        // report how many properties a schema's instances declare, which sizes a full warm-up pass (§28.2)
        std::size_t count_of_properties(const std::vector<albor::prototype_d2::ValueType>& types) { return types.size() * INSTANCES; }

        // hold what one schema's measurement produced (§28.2)
        struct SchemaResult
        {
            std::string name;
            PathSamples typed_global;
            PathSamples erased_global;
            PathSamples typed_base_offset;
            PathSamples typed_value;
            PathSamples typed_string;
            PathSamples floor;
        };

        // measure one schema: every path, interleaved, over the committed run and block counts (§28.4)
        SchemaResult measure_schema(const std::string& schema_name, const std::vector<albor::prototype_d2::ValueType>& types, const bool has_string, const std::size_t warmup_reads) {
            SchemaResult result;
            result.name = schema_name;

            // the typed store, on its own region table, because a shared one inflated D2's figures (R1's correction)
            albor::prototype_d2::RegionTable typed_regions;
            const albor::prototype_d2::SchemaId typed_schema = typed_regions.add_schema(schema_name, albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore typed(typed_regions);

            // the erased store, on its own region table (§28.2)
            albor::prototype_d2::RegionTable erased_regions;
            const albor::prototype_d2::SchemaId erased_schema = erased_regions.add_schema(schema_name, albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::ErasedStore erased(erased_regions);

            // the local index of the string property in the mixed schema, which is where the payload path reads (§13.1)
            const albor::prototype_d2::LocalPropertyId string_local = 3;

            // fill both stores over the committed property space, so every read touches a live value (§28.2)
            for (std::size_t instance = 0; instance < INSTANCES; instance++) {
                const albor::prototype_d2::InstanceId typed_instance = typed.create(typed_schema);
                const albor::prototype_d2::InstanceId erased_instance = erased.create(erased_schema);

                for (albor::prototype_d2::LocalPropertyId local = 0; local < types.size(); local++) {
                    const std::int64_t seed = static_cast<std::int64_t>(instance * 8 + local);

                    // seed a scalar property with an integer, which is what the fast paths read (§13.1)
                    if (types.at(local) == albor::prototype_d2::ValueType::Int) {
                        static_cast<void>(typed.write(typed_regions.global(typed_instance, local), albor::prototype_d2::Value(seed), albor::prototype_d2::WriteSource::GeneratedInit));
                        static_cast<void>(erased.write(erased_regions.global(erased_instance, local), albor::prototype_d2::Value(seed), albor::prototype_d2::WriteSource::GeneratedInit));
                        continue;
                    }

                    // seed every other declared type with its own default, so the memory is representative (§13.1)
                    const albor::prototype_d2::Value seeded = types.at(local) == albor::prototype_d2::ValueType::Double ? albor::prototype_d2::Value(static_cast<double>(seed)) : types.at(local) == albor::prototype_d2::ValueType::Bool ? albor::prototype_d2::Value(seed % 2 == 0) : albor::prototype_d2::Value();
                    static_cast<void>(typed.write(typed_regions.global(typed_instance, local), seeded, albor::prototype_d2::WriteSource::GeneratedInit));
                    static_cast<void>(erased.write(erased_regions.global(erased_instance, local), seeded, albor::prototype_d2::WriteSource::GeneratedInit));
                }

                // give the string property a real payload, so the payload path copies a live shared_ptr (§13.1)
                if (has_string) {
                    static_cast<void>(typed.write(typed_regions.global(typed_instance, string_local), albor::prototype_d2::Value::make_string("a shared immutable payload"), albor::prototype_d2::WriteSource::GeneratedInit));
                    static_cast<void>(erased.write(erased_regions.global(erased_instance, string_local), albor::prototype_d2::Value::make_string("a shared immutable payload"), albor::prototype_d2::WriteSource::GeneratedInit));
                }
            }

            const ReadOrder order = build_order(typed_regions);
            const std::size_t count = order.size();

            result.typed_global.name = "A typed read_int, global PropertyId";
            result.erased_global.name = "B erased read_int, global PropertyId";
            result.typed_base_offset.name = "C typed read_int, (instance, local) base offset";
            result.typed_value.name = "D typed read() returning Value by value";
            result.typed_string.name = "E typed string read, shared_ptr copy";
            result.floor.name = "- empty loop over the same shuffled order";

            // discard the committed warm-up reads, which pay the first touch of the first pages only (§28.4)
            for (std::size_t index = 0; index < warmup_reads; index++) {
                g_sink += static_cast<std::uint64_t>(typed.read_int(order.at(index % count).id));
            }

            // walk the runs, and inside each run walk the paths block by block so drift hits them equally (§28.4)
            for (std::size_t run = 0; run < RUNS; run++) {
                const std::size_t first = (run * READS_PER_RUN) % count;

                for (std::size_t block = 0; block < READS_PER_RUN; block += BLOCK) {
                    const std::size_t begin = (first + block) % count;
                    const std::size_t end = std::min(count, begin + BLOCK);

                    // path A: the shipping path, a dense id read through the scalar fast path (§13.3)
                    {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            g_sink += static_cast<std::uint64_t>(typed.read_int(order.at(index).id));
                        }

                        result.typed_global.samples.push_back(time_block_now() - start);
                    }

                    // path B: the same read against the erased slot (§13.3)
                    {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            g_sink += static_cast<std::uint64_t>(erased.read_int(order.at(index).id));
                        }

                        result.erased_global.samples.push_back(time_block_now() - start);
                    }

                    // path C: the same read through the per-instance base offset (AD-04a)
                    {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            const albor::prototype_d2::Region& region = typed_regions.region(order.at(index).instance);
                            g_sink += static_cast<std::uint64_t>(typed.read_int(region.first_id + order.at(index).local));
                        }

                        result.typed_base_offset.samples.push_back(time_block_now() - start);
                    }

                    // path D: the read D2 measured, which builds the 40 B variant before extracting the integer (§13.3)
                    {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            g_sink += static_cast<std::uint64_t>(typed.read(order.at(index).id).as_int());
                        }

                        result.typed_value.samples.push_back(time_block_now() - start);
                    }

                    // path E: the payload path, which copies a shared_ptr (§13.1)
                    if (has_string) {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            const albor::prototype_d2::Region& region = typed_regions.region(order.at(index).instance);
                            g_sink += typed.read_string(region.first_id + string_local) ? 1u : 0u;
                        }

                        result.typed_string.samples.push_back(time_block_now() - start);
                    }

                    // the floor: the same loop and the same array traffic with no store access at all (§28.2)
                    {
                        const double start = time_block_now();

                        for (std::size_t index = begin; index < end; index++) {
                            g_sink += order.at(index).id;
                        }

                        result.floor.samples.push_back(time_block_now() - start);
                    }
                }

                // record each path's median block for this run, which is what the spread is computed over (§28.2)
                const auto record_run = [](PathSamples& path) {
                    const std::size_t first_block = path.samples.size() - (READS_PER_RUN / BLOCK);
                    std::vector<double> block_medians;

                    for (std::size_t block = first_block; block < path.samples.size(); block++) {
                        block_medians.push_back(path.samples.at(block));
                    }

                    path.run_p50.push_back(percentile(std::move(block_medians), 0.50));
                };

                record_run(result.typed_global);
                record_run(result.erased_global);
                record_run(result.typed_base_offset);
                record_run(result.typed_value);
                record_run(result.floor);

                if (has_string) {
                    record_run(result.typed_string);
                }
            }

            // hand back what this schema's paths produced (§28.2)
            return result;
        }

        // report one path's per-run medians and its spread, which is the deliverable of this revision (§28.2)
        void report_path(const PathSamples& path) {
            std::printf("  %-44s", path.name.c_str());

            for (const double median : path.run_p50) {
                std::printf(" %8s", format(ns_per_read(median), 2).c_str());
            }

            std::printf("   spread %6s%%\n", format(spread_of(path) * 100.0, 1).c_str());
        }

        // judge one schema's paths against the committed thresholds (§28.4)
        void judge_schema(Verdict& verdict, const SchemaResult& result) {
            const std::string suffix = " [" + result.name + "]";

            // T3.10.1: the spread bound gates every other threshold in this revision (§28.4)
            const double typed_spread = spread_of(result.typed_global);
            const double erased_spread = spread_of(result.erased_global);
            const double offset_spread = spread_of(result.typed_base_offset);
            const double value_spread = spread_of(result.typed_value);
            check(verdict, "T3.10.1", "run-to-run spread of the p50 <= 10%" + suffix, typed_spread <= T3_10_1_SPREAD && erased_spread <= T3_10_1_SPREAD && offset_spread <= T3_10_1_SPREAD && value_spread <= T3_10_1_SPREAD, "typed " + format(typed_spread * 100.0, 1) + "%, erased " + format(erased_spread * 100.0, 1) + "%, base offset " + format(offset_spread * 100.0, 1) + "%, Value " + format(value_spread * 100.0, 1) + "%");

            // T3.10.2: the typed scalar read's absolute p99 and its ratio to the erased slot (§28.4)
            const double typed_p99 = p99_of(result.typed_global);
            const double erased_p99 = p99_of(result.erased_global);
            check(verdict, "T3.10.2", "typed read p99 <= 20 ns and <= 1.5x erased" + suffix, typed_p99 <= T3_10_2_TYPED_P99_NS && typed_p99 <= erased_p99 * T3_10_2_ERASED_RATIO, format(typed_p99, 2) + " ns vs erased " + format(erased_p99, 2) + " ns, budget " + format(T3_10_2_TYPED_P99_NS, 0) + " ns and " + format(T3_10_2_ERASED_RATIO, 1) + "x");

            // T3.10.3: the base-offset indirection against D2's 2.3 % (§28.4)
            const double indirection = (mean_of(result.typed_base_offset) - mean_of(result.typed_global)) / mean_of(result.typed_global);
            check(verdict, "T3.10.3", "base-offset indirection <= 5% of the typed read p50" + suffix, indirection <= T3_10_3_INDIRECTION, format(indirection * 100.0, 2) + "% <= " + format(T3_10_3_INDIRECTION * 100.0, 0) + "% (D2 recorded 2.3%)");

            // T3.10.4: the scalar fast path against the variant copy, stated as a claim to be tested (§28.4)
            const double ratio = mean_of(result.typed_value) / mean_of(result.typed_global);
            check(verdict, "T3.10.4", "scalar fast path >= 2x cheaper than a Value read" + suffix, ratio >= T3_10_4_FAST_PATH_RATIO, format(ratio, 2) + "x >= " + format(T3_10_4_FAST_PATH_RATIO, 1) + "x (Value read " + format(mean_of(result.typed_value), 2) + " ns, fast path " + format(mean_of(result.typed_global), 2) + " ns)");

            // T3.10.5: the decomposition is the deliverable (§28.4)
            record("T3.10.5", "read path decomposition" + suffix, "fast path " + format(mean_of(result.typed_global), 2) + " ns, base offset " + format(mean_of(result.typed_base_offset), 2) + " ns, Value copy " + format(mean_of(result.typed_value), 2) + " ns, erased " + format(mean_of(result.erased_global), 2) + " ns, loop floor " + format(mean_of(result.floor), 2) + " ns");
        }

    } // namespace

    void run_read_path(Verdict& verdict) {
        // both schemas R1 measured, so the read path is not reported for one mix only (§28.4)
        const std::vector<albor::prototype_d2::ValueType> scalar_types(8, albor::prototype_d2::ValueType::Int);
        const std::vector<albor::prototype_d2::ValueType> mixed_types{
            albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double, albor::prototype_d2::ValueType::Bool, albor::prototype_d2::ValueType::String, albor::prototype_d2::ValueType::TranslatedString, albor::prototype_d2::ValueType::Snapshot, albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double,
        };

        // measure the scalar schema first, which is the common case for a layout pass (§13.1)
        const SchemaResult scalar = measure_schema("scalar8", scalar_types, false, WARMUP_READS);

        // then the mixed schema, which is where the payload path exists (§13.1)
        const SchemaResult mixed = measure_schema("mixed8", mixed_types, true, WARMUP_READS);

        // report every path's per-run median, in nanoseconds per read (§28.2)
        std::printf("--- interleaved read path, ns per read, one column per run ---\n");
        std::printf("  %-44s %8s %8s %8s %8s %8s\n", "path", "run1", "run2", "run3", "run4", "run5");

        for (const SchemaResult* schema : {&scalar, &mixed}) {
            std::printf("  schema %s\n", schema->name.c_str());
            report_path(schema->typed_global);
            report_path(schema->erased_global);
            report_path(schema->typed_base_offset);
            report_path(schema->typed_value);

            if (schema == &mixed) {
                report_path(schema->typed_string);
            }

            report_path(schema->floor);
        }

        // judge the scalar schema, then the mixed one, against the committed thresholds (§28.4)
        judge_schema(verdict, scalar);
        judge_schema(verdict, mixed);

        // the steady-state diagnostic: the same program with one full warm-up pass over the property space before
        // the first run, which is not the committed protocol and is therefore reported, never thresholded (§28.4)
        std::printf("\n--- steady-state diagnostic, NOT the committed protocol: one full warm-up pass first ---\n");
        std::printf("  %-44s %8s %8s %8s %8s %8s\n", "path", "run1", "run2", "run3", "run4", "run5");

        const SchemaResult warm_scalar = measure_schema("scalar8", scalar_types, false, count_of_properties(scalar_types) * INSTANCES);
        const SchemaResult warm_mixed = measure_schema("mixed8", mixed_types, true, count_of_properties(mixed_types) * INSTANCES);

        for (const SchemaResult* schema : {&warm_scalar, &warm_mixed}) {
            std::printf("  schema %s\n", schema->name.c_str());
            report_path(schema->typed_global);
            report_path(schema->erased_global);
            report_path(schema->typed_base_offset);
            report_path(schema->typed_value);

            if (schema == &warm_mixed) {
                report_path(schema->typed_string);
            }

            report_path(schema->floor);
        }

        // report what the warm machine would have said about each committed threshold, without judging it (§28.4)
        record("-", "steady state: fastest run per path", "scalar fast path " + format(min_of(warm_scalar.typed_global), 2) + " ns, base offset " + format(min_of(warm_scalar.typed_base_offset), 2) + " ns, Value " + format(min_of(warm_scalar.typed_value), 2) + " ns, erased " + format(min_of(warm_scalar.erased_global), 2) + " ns, floor " + format(min_of(warm_scalar.floor), 2) + " ns");
        record("-", "steady state: fastest-run base-offset delta", format(100.0 * (min_of(warm_scalar.typed_base_offset) - min_of(warm_scalar.typed_global)) / min_of(warm_scalar.typed_global), 2) + "% scalar, " + format(100.0 * (min_of(warm_mixed.typed_base_offset) - min_of(warm_mixed.typed_global)) / min_of(warm_mixed.typed_global), 2) + "% mixed");
        record("-", "steady state: spreads", "scalar typed " + format(spread_of(warm_scalar.typed_global) * 100.0, 1) + "%, base offset " + format(spread_of(warm_scalar.typed_base_offset) * 100.0, 1) + "%, mixed typed " + format(spread_of(warm_mixed.typed_global) * 100.0, 1) + "%");
        record("-", "steady state: base-offset delta", format(100.0 * (mean_of(warm_scalar.typed_base_offset) - mean_of(warm_scalar.typed_global)) / mean_of(warm_scalar.typed_global), 2) + "% scalar, " + format(100.0 * (mean_of(warm_mixed.typed_base_offset) - mean_of(warm_mixed.typed_global)) / mean_of(warm_mixed.typed_global), 2) + "% mixed, against D2's recorded 2.3%");
        record("-", "steady state: Value copy ratio", format(mean_of(warm_scalar.typed_value) / mean_of(warm_scalar.typed_global), 2) + "x scalar, " + format(mean_of(warm_mixed.typed_value) / mean_of(warm_mixed.typed_global), 2) + "x mixed");

        // report the sink, so a reader can see the reads were not optimised away (§28.2)
        record("-", "read accumulator", std::to_string(g_sink));
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R10 — the read path, interleaved A/B with spread (AD-04a, AD-04b)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R10, committed before this program existed\n");
    std::printf("%zu runs of %zu reads per path per run, interleaved in blocks of %zu, after %zu warm-up reads\n\n", albor::prototype_d3::RUNS, albor::prototype_d3::READS_PER_RUN, albor::prototype_d3::BLOCK, albor::prototype_d3::WARMUP_READS);

    std::printf("machine load average at start: %.2f\n", albor::prototype_d3::load_average());

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_read_path(verdict);

    std::printf("machine load average at end: %.2f\n", albor::prototype_d3::load_average());

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
