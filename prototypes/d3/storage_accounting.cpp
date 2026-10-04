#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"

namespace albor::prototype_d3
{

    namespace
    {

        // -----------------------------------------------------------------------------------------------------------
        // the thresholds below are transcribed from prototypes/d3/plan.md, which was committed before this program
        // existed (§28.4). Do not edit a value here to change a verdict.
        // -----------------------------------------------------------------------------------------------------------

        // T3.1.2 typed scalar total bytes per property
        constexpr double T3_1_2_SCALAR_BYTES = 24.0;

        // T3.1.3 typed mixed total bytes per property
        constexpr double T3_1_3_MIXED_BYTES = 32.0;

        // T3.1.4 typed string-heavy total bytes per property
        constexpr double T3_1_4_STRING_HEAVY_BYTES = 64.0;

        // T3.1.5 the four columns must sum to the reported total within this many bytes
        constexpr double T3_1_5_CLOSURE_BYTES = 0.01;

        // T3.1.6 the erased cost must not vary with the mix by more than this
        constexpr double T3_1_6_ERASED_SPREAD_BYTES = 0.5;

        // T3.1.7 typed slab slack as a share of allocated slab bytes
        constexpr double T3_1_7_SLACK_SHARE = 0.25;

        // the instance counts the scaling table reports (§28.2)
        constexpr std::size_t INSTANCE_COUNTS[] = {1, 64, 4096};

        // a finer sweep to locate where typed storage overtakes the erased slot (§28.2)
        constexpr std::size_t CROSSOVER_COUNTS[] = {128, 256, 512, 1024, 2048, 4096, 8192};

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;

            // collect the erased bytes per property of every mix, so T3.1.6 can judge the spread across them (§28.4)
            std::vector<double> erased_bytes_per_property;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-8s %-56s %-6s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-8s %-56s %-6s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        std::string format(double value, int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // the four columns of the accounting table, plus the total they must sum to (§28.2)
        struct Accounting
        {
            double value = 0.0;
            double addressability = 0.0;
            double slack = 0.0;
            double region_overhead = 0.0;
            double total = 0.0;
            double properties = 0.0;

            // predict the value bytes from the schemas alone, without asking the store (§28.4)
            double predicted_value = 0.0;

            // predict the addressability bytes from the property count alone (§28.4)
            double predicted_address = 0.0;
        };

        // measure the typed candidate over one schema and one instance count (§28.2)
        Accounting measure_typed(albor::prototype_d2::RegionTable& regions, albor::prototype_d2::TypedStore& store, albor::prototype_d2::SchemaId schema, std::size_t instances) {
            // accumulate an accounting row (§28.2)
            Accounting row;

            // create the live instances the measurement is over (§5)
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }

            // take the denominator from this candidate's own property count, never a constant (§28.4)
            row.properties = static_cast<double>(store.property_count());

            // the inline bytes the schema laid out (§13.3)
            row.value = static_cast<double>(store.value_bytes());

            // the per-property entries a dense id space needs: location, type, source and binding attachment (§5)
            row.addressability = static_cast<double>(store.address_bytes());

            // predict the value bytes by summing each live region's declared size, independently of the store (§28.4)
            for (std::size_t index = 0; index < regions.region_count(); index++) {
                // a destroyed region holds no bytes (§5)
                if (!regions.alive(static_cast<albor::prototype_d2::InstanceId>(index))) {
                    continue;
                }

                // add the bytes this component type's schema declared (§13.3)
                row.predicted_value += static_cast<double>(regions.schema(regions.region(static_cast<albor::prototype_d2::InstanceId>(index)).schema).region_bytes);
            }

            // predict the addressability bytes as one location, type, source and attachment byte per property (§5)
            row.predicted_address = static_cast<double>(store.property_count()) * (sizeof(std::uint32_t) + 3 * sizeof(std::uint8_t));

            // what chunked allocation holds beyond the live regions (§13.3)
            row.slack = static_cast<double>(store.slab_allocated_bytes()) - row.value;

            // what the region table and the base-offset entries cost, which is per instance (§5)
            row.region_overhead = static_cast<double>(store.region_table_bytes() + store.region_base_bytes());

            // the total is the sum of the columns, so the table can be checked against itself (§28.4)
            row.total = row.value + row.addressability + row.slack + row.region_overhead;

            // the region table is only a reference for the measurement and must outlive the store
            static_cast<void>(regions);

            // hand back the row (§28.2)
            return row;
        }

        // measure the erased candidate over the same declared schema (§28.4)
        Accounting measure_erased(albor::prototype_d2::RegionTable& regions, albor::prototype_d2::ErasedStore& store, albor::prototype_d2::SchemaId schema, std::size_t instances) {
            // accumulate an accounting row (§28.2)
            Accounting row;

            // create the live instances the measurement is over (§5)
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }

            // take the denominator from this candidate's own property count (§28.4)
            row.properties = static_cast<double>(store.property_count());

            // the uniform variant is the whole value cost, and it cannot depend on the declared mix (§13.3)
            row.value = static_cast<double>(store.memory_bytes());

            // predict the slot bytes from the slot size and the property count alone (§28.4)
            row.predicted_value = static_cast<double>(store.property_count()) * albor::prototype_d2::ErasedStore::bytes_per_property();

            // the slot already carries the write source and the attachment, so there is no separate column (§13.3)
            row.addressability = 0.0;

            // what the slot array's capacity holds beyond the slots in use (§28.2)
            row.slack = static_cast<double>(store.slot_capacity_bytes()) - row.value;

            // what the region table costs, which is per instance (§5)
            row.region_overhead = static_cast<double>(regions.region_table_bytes());

            // the total is the sum of the columns (§28.4)
            row.total = row.value + row.addressability + row.slack + row.region_overhead;

            // hand back the row (§28.2)
            return row;
        }

        // divide every column by the property count so the row reads in bytes per property (§28.2)
        Accounting per_property(Accounting row) {
            // a store with no properties cannot be normalised (§28.2)
            if (row.properties <= 0.0) {
                return row;
            }

            // scale every column by the property count (§28.2)
            row.value /= row.properties;
            row.addressability /= row.properties;
            row.slack /= row.properties;
            row.region_overhead /= row.properties;
            row.total /= row.properties;
            row.properties = 1.0;

            // hand back the normalised row (§28.2)
            return row;
        }

        // print one candidate's row as a table line (§28.2)
        void print_row(std::string_view label, const Accounting& row) {
            // the columns are value, addressability, slack, region overhead and total (§28.4)
            std::printf("  %-12s value=%7s  addr=%6s  slack=%8s  region=%7s  total=%8s\n", std::string(label).c_str(), format(row.value).c_str(), format(row.addressability).c_str(), format(row.slack).c_str(), format(row.region_overhead).c_str(), format(row.total).c_str());
        }

    } // namespace

    // measure the typed and erased candidates over one schema, each with its own region table (§28.4)
    void run_mix(std::string_view mix, const std::vector<albor::prototype_d2::ValueType>& types, Verdict& verdict) {
        // announce the mix (§28.2)
        std::printf("\n--- %s: %zu properties ---\n", std::string(mix).c_str(), types.size());

        // measure the typed candidate alone, so neither store can inflate the other's property count (§28.4)
        albor::prototype_d2::RegionTable typed_regions;
        const albor::prototype_d2::SchemaId typed_schema = typed_regions.add_schema("Typed", albor::prototype_d2::InstanceKind::Component, types);
        albor::prototype_d2::TypedStore typed(typed_regions);
        const Accounting typed_raw = measure_typed(typed_regions, typed, typed_schema, 1);
        const Accounting typed_row = per_property(typed_raw);

        // measure the erased candidate over the same declared types, on its own region table (§28.4)
        albor::prototype_d2::RegionTable erased_regions;
        const albor::prototype_d2::SchemaId erased_schema = erased_regions.add_schema("Erased", albor::prototype_d2::InstanceKind::Component, types);
        albor::prototype_d2::ErasedStore erased(erased_regions);
        const Accounting erased_raw = measure_erased(erased_regions, erased, erased_schema, 1);
        const Accounting erased_row = per_property(erased_raw);

        // print both rows (§28.2)
        std::printf("  bytes per property, one instance:\n");
        print_row("typed", typed_row);
        print_row("erased", erased_row);

        // T3.1.5: the accounting must match figures predicted from the schemas and the slot size, independently of
        // the stores that produced it. Checking the columns against their own sum would be tautological (§28.4)
        const double typed_value_error = std::abs(typed_raw.value - typed_raw.predicted_value);
        const double typed_address_error = std::abs(typed_raw.addressability - typed_raw.predicted_address);
        const double erased_value_error = std::abs(erased_raw.value - erased_raw.predicted_value);

        // T3.1.1: typed must not cost more than erased on any mix (§28.4)
        check(verdict, "T3.1.1", std::string(mix) + ": typed total <= erased total", typed_row.total <= erased_row.total, format(typed_row.total) + " B <= " + format(erased_row.total) + " B");

        // report the ratio, which is the finding rather than a threshold (§28.4)
        record("T3.1.8", std::string(mix) + ": erased / typed total", erased_row.total > 0.0 ? format(erased_row.total / typed_row.total) + "x" : "n/a");

        // T3.1.2 to T3.1.4: the per-mix absolute budgets (§28.4)
        if (mix == "scalar") {
            check(verdict, "T3.1.2", "typed scalar total bytes/property", typed_row.total <= T3_1_2_SCALAR_BYTES, format(typed_row.total) + " B <= " + format(T3_1_2_SCALAR_BYTES, 0) + " B");
        }

        if (mix == "mixed") {
            check(verdict, "T3.1.3", "typed mixed total bytes/property", typed_row.total <= T3_1_3_MIXED_BYTES, format(typed_row.total) + " B <= " + format(T3_1_3_MIXED_BYTES, 0) + " B");
        }

        if (mix == "string_heavy") {
            check(verdict, "T3.1.4", "typed string_heavy total bytes/property", typed_row.total <= T3_1_4_STRING_HEAVY_BYTES, format(typed_row.total) + " B <= " + format(T3_1_4_STRING_HEAVY_BYTES, 0) + " B");
        }

        // T3.1.5: the measured value bytes must equal the sum of the live regions' declared sizes (§28.4)
        check(verdict, "T3.1.5a", std::string(mix) + ": typed value bytes == sum of live region_bytes", typed_value_error <= T3_1_5_CLOSURE_BYTES, format(typed_value_error, 4) + " B error <= " + format(T3_1_5_CLOSURE_BYTES, 2) + " B (" + format(typed_raw.value) + " measured vs " + format(typed_raw.predicted_value) + " predicted)");

        // T3.1.5: the measured addressability bytes must equal 7 per property (§28.4)
        check(verdict, "T3.1.5b", std::string(mix) + ": typed addressability == 7 B x properties", typed_address_error <= T3_1_5_CLOSURE_BYTES, format(typed_address_error, 4) + " B error <= " + format(T3_1_5_CLOSURE_BYTES, 2) + " B (" + format(typed_raw.addressability) + " measured vs " + format(typed_raw.predicted_address) + " predicted)");

        // T3.1.5: the measured slot bytes must equal the slot size times the property count (§28.4)
        check(verdict, "T3.1.5c", std::string(mix) + ": erased slot bytes == sizeof(slot) x properties", erased_value_error <= T3_1_5_CLOSURE_BYTES, format(erased_value_error, 4) + " B error <= " + format(T3_1_5_CLOSURE_BYTES, 2) + " B (" + format(erased_raw.value) + " measured vs " + format(erased_raw.predicted_value) + " predicted)");

        // collect the erased cost so T3.1.6 can be judged across mixes (§28.4)
        verdict.erased_bytes_per_property.push_back(erased_row.total);
        record("-", std::string(mix) + ": erased total bytes/property", format(erased_row.total) + " B (a uniform variant, so it must not vary with the mix)");
    }

    // report how the per-instance costs scale with the number of live instances (§28.2)
    void run_scaling(Verdict& verdict) {
        // announce the scaling table (§28.2)
        std::printf("\n--- scaling with live instance count, mixed schema ---\n");

        // the mix whose per-instance overhead is reported (§13.1)
        const std::vector<albor::prototype_d2::ValueType> types{
            albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double, albor::prototype_d2::ValueType::Bool, albor::prototype_d2::ValueType::String, albor::prototype_d2::ValueType::TranslatedString, albor::prototype_d2::ValueType::Snapshot, albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double,
        };

        std::printf("  %-10s %-8s %-10s %-10s %-10s %-10s\n", "instances", "props", "typed B/p", "erased B/p", "typed tot", "erased tot");

        // the largest instance count, so T3.1.7 is judged where the slab is under pressure (§28.4)
        Accounting largest_typed;

        for (const std::size_t instances : INSTANCE_COUNTS) {
            // measure the typed candidate at this instance count (§28.2)
            albor::prototype_d2::RegionTable typed_regions;
            const albor::prototype_d2::SchemaId typed_schema = typed_regions.add_schema("Typed", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore typed(typed_regions);
            const Accounting typed_row = measure_typed(typed_regions, typed, typed_schema, instances);

            // measure the erased candidate at the same instance count (§28.4)
            albor::prototype_d2::RegionTable erased_regions;
            const albor::prototype_d2::SchemaId erased_schema = erased_regions.add_schema("Erased", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::ErasedStore erased(erased_regions);
            const Accounting erased_row = measure_erased(erased_regions, erased, erased_schema, instances);

            // print the row before normalising it (§28.2)
            std::printf("  %-10zu %-8.0f %-10s %-10s %-10.0f %-10.0f\n", instances, typed_row.properties, format(per_property(typed_row).total).c_str(), format(per_property(erased_row).total).c_str(), typed_row.total, erased_row.total);

            // remember the largest typed measurement so the slack share is judged at scale (§28.4)
            if (instances == INSTANCE_COUNTS[std::size(INSTANCE_COUNTS) - 1]) {
                largest_typed = typed_row;
            }
        }

        // report the crossover, which is where the typed total first falls below the erased total (§28.2)
        std::printf("\n--- crossover: mixed schema, where typed stops losing ---\n");
        std::printf("  %-10s %-8s %-12s %-12s %-10s\n", "instances", "props", "typed total", "erased total", "erased/typed");

        // remember the first instance count at which typed wins (§28.2)
        std::size_t crossover_instances = 0;

        for (const std::size_t instances : CROSSOVER_COUNTS) {
            // measure the typed candidate at this instance count (§28.2)
            albor::prototype_d2::RegionTable typed_regions;
            const albor::prototype_d2::SchemaId typed_schema = typed_regions.add_schema("Typed", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore typed(typed_regions);
            const Accounting typed_row = measure_typed(typed_regions, typed, typed_schema, instances);

            // measure the erased candidate at the same instance count (§28.4)
            albor::prototype_d2::RegionTable erased_regions;
            const albor::prototype_d2::SchemaId erased_schema = erased_regions.add_schema("Erased", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::ErasedStore erased(erased_regions);
            const Accounting erased_row = measure_erased(erased_regions, erased, erased_schema, instances);

            // record the first count at which the typed total is the smaller of the two (§28.2)
            if (crossover_instances == 0 && typed_row.total <= erased_row.total) {
                crossover_instances = instances;
            }

            std::printf("  %-10zu %-8.0f %-12.0f %-12.0f %-10s\n", instances, typed_row.properties, typed_row.total, erased_row.total, format(erased_row.total / typed_row.total).c_str());
        }

        record("T3.1.9", "crossover instance count, mixed schema", crossover_instances == 0 ? std::string("not reached up to ") + std::to_string(CROSSOVER_COUNTS[std::size(CROSSOVER_COUNTS) - 1]) + " instances" : "typed storage first becomes smaller at " + std::to_string(crossover_instances) + " instances = " + std::to_string(crossover_instances * 8) + " properties");

        // T3.1.7: the slack chunked allocation leaves must stay a modest share of what it allocated (§28.4)
        const double allocated = static_cast<double>(largest_typed.value + largest_typed.slack);
        const double share = allocated > 0.0 ? largest_typed.slack / allocated : 0.0;

        check(verdict, "T3.1.7", "typed slab slack / allocated, at 4096 instances", share <= T3_1_7_SLACK_SHARE, format(share * 100.0) + "% <= " + format(T3_1_7_SLACK_SHARE * 100.0, 0) + "% (" + format(static_cast<double>(largest_typed.slack), 0) + " B slack of " + format(allocated, 0) + " B)");
    }

    // judge T3.1.6: the erased cost must be one number across every mix, because the slot is a uniform variant (§28.4)
    void check_erased_uniformity(Verdict& verdict) {
        // a single mix cannot show a spread (§28.4)
        if (verdict.erased_bytes_per_property.size() < 2) {
            record("T3.1.6", "erased uniformity across mixes", "needs at least two mixes");
            return;
        }

        // find the extremes the spread is measured between (§28.4)
        const auto [lowest, highest] = std::minmax_element(verdict.erased_bytes_per_property.begin(), verdict.erased_bytes_per_property.end());

        // the spread is the difference between the cheapest and dearest mix (§28.4)
        const double spread = *highest - *lowest;

        // a uniform variant cannot cost more for one declared mix than another, so any spread is a measurement fault (§28.4)
        check(verdict, "T3.1.6", "erased bytes/property identical across all mixes", spread <= T3_1_6_ERASED_SPREAD_BYTES, format(spread, 4) + " B spread <= " + format(T3_1_6_ERASED_SPREAD_BYTES, 1) + " B (min " + format(*lowest) + ", max " + format(*highest) + ")");
    }

    // report the erased slot's composition as measured, not hand-derived (§28.4)
    void report_composition(Verdict& verdict) {
        // record why T3.1.5 is measured against predictions rather than against the column sum (§28.4)
        record("T3.1.5", "why the check is against predictions", "plan.md worded T3.1.5 as 'the columns sum to the total', which is tautological because total is defined as that sum; it is measured here against the schema table and the slot size instead, which can fail");
        // announce the composition (§28.2)
        std::printf("\n--- measured type sizes ---\n");

        // report the sizes the value model actually compiles to (§28.4)
        std::printf("  sizeof(Value variant)      %zu B\n", albor::prototype_d2::Value::slot_bytes());
        std::printf("  sizeof(SnapshotBox)        %zu B\n", sizeof(albor::prototype_d2::SnapshotBox));
        std::printf("  sizeof(TranslatedString)   %zu B\n", sizeof(albor::prototype_d2::TranslatedString));
        std::printf("  sizeof(Region)             %zu B\n", sizeof(albor::prototype_d2::Region));
        std::printf("  SLAB_CHUNK_BYTES           %zu B\n", albor::prototype_d2::SLAB_CHUNK_BYTES);

        // report the ratio of the largest alternative to the slot, which is what the variant is paying for (§28.4)
        record("-", "variant payload share", format(100.0 * static_cast<double>(sizeof(albor::prototype_d2::SnapshotBox)) / static_cast<double>(albor::prototype_d2::Value::slot_bytes()), 1) + "% of the variant is the largest alternative alone");

        // report whether the three erased measurements agreed, which the caller judges (§28.4)
        static_cast<void>(verdict);
    }

} // namespace albor::prototype_d3

int main() {
    using namespace albor::prototype_d2;
    using albor::prototype_d3::Verdict;

    // announce the configuration (§28.2)
    std::printf("Prototype D3 R1 — typed storage accounting (AD-04b)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md, committed before this program existed\n");
    std::printf("memory measurement is deterministic: single figures, no spread\n");

    // keep the tally across every mix (§28.4)
    Verdict verdict;

    // the eight Int properties that stand for a numeric component (§28.2)
    const std::vector<ValueType> scalar(8, ValueType::Int);

    // one of every value category the design names (§13.1)
    const std::vector<ValueType> mixed{
        ValueType::Int, ValueType::Double, ValueType::Bool, ValueType::String, ValueType::TranslatedString, ValueType::Snapshot, ValueType::Int, ValueType::Double,
    };

    // labels and text, where the pointer payloads dominate (§13.1)
    const std::vector<ValueType> string_heavy{
        ValueType::String, ValueType::TranslatedString, ValueType::String, ValueType::TranslatedString, ValueType::String, ValueType::TranslatedString, ValueType::String, ValueType::TranslatedString,
    };

    // measure each mix on its own region tables (§28.4)
    albor::prototype_d3::run_mix("scalar", scalar, verdict);
    albor::prototype_d3::run_mix("mixed", mixed, verdict);
    albor::prototype_d3::run_mix("string_heavy", string_heavy, verdict);

    // report how the per-instance costs scale (§28.2)
    albor::prototype_d3::run_scaling(verdict);

    // judge the erased uniformity across every mix that was measured (§28.4)
    albor::prototype_d3::check_erased_uniformity(verdict);

    // report the composition as measured (§28.4)
    albor::prototype_d3::report_composition(verdict);

    // report the overall result (§28.4)
    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
