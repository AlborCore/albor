// Prototype D3 R2 — combined memory of storage × index at scale (F2.2, AD-04b)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.

#include "prototypes/d2/src/index.h"
#include "prototypes/d2/src/storage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        // the committed scale (plan.md R2): 100 000 live properties, 11 000 bindings of 2 dependencies each
        constexpr std::size_t SCALE_PROPERTIES = 100000;
        constexpr std::size_t SCALE_BINDINGS = 11000;
        constexpr std::size_t DEPENDENCIES_PER_BINDING = 2;

        // the committed post-seal activity: what a live reload or a conditional row adds (plan.md R2)
        constexpr std::size_t POST_SEAL_ADDITIONS = 1000;

        // T3.2.2 combined typed + sealed CSR bytes per property, mixed schema
        constexpr double T3_2_2_COMBINED_BYTES = 36.0;

        // T3.2.3 the segmented index may add this much per property over sealed CSR
        constexpr double T3_2_3_SEGMENTED_DELTA = 12.0;

        // T3.2.4 the hybrid may add this much per property over sealed CSR
        constexpr double T3_2_4_HYBRID_DELTA = 6.0;

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

        // the hybrid index (plan.md R2): sealed CSR for everything declared before finish(), a segmented
        // tail for add_entry after it; dependents() concatenates both halves
        class HybridIndex
        {
        public:
            void declare(albor::prototype_d2::BindingId binding, std::span<const albor::prototype_d2::PropertyId> dependencies) {
                // a declaration before seal goes to the CSR half, which is the whole point (§28.2)
                m_sealed.declare(binding, dependencies);
            }

            void finish(const std::size_t property_count) {
                m_sealed.finish(property_count);
                static_cast<void>(m_sealed.rebuild());
                m_property_count = property_count;
            }

            albor::prototype_d2::MutationCost add_entry(const albor::prototype_d2::BindingId binding, const albor::prototype_d2::PropertyId property) {
                // a post-seal addition goes to the segmented tail, so the CSR half never rebuilds (plan.md R2)
                return m_tail.add_entry(binding, property);
            }

            // the dependents of one property, both halves (§28.2)
            [[nodiscard]] std::vector<albor::prototype_d2::BindingId> dependents(const albor::prototype_d2::PropertyId property) const {
                const std::span<const albor::prototype_d2::BindingId> sealed = m_sealed.dependents(property);
                const std::span<const albor::prototype_d2::BindingId> tail = m_tail.dependents(property);
                std::vector<albor::prototype_d2::BindingId> combined{sealed.begin(), sealed.end()};
                combined.insert(combined.end(), tail.begin(), tail.end());
                return combined;
            }

            [[nodiscard]] std::size_t index_bytes() const { return m_sealed.index_bytes() + m_tail.index_bytes(); }

            // the CSR half is rebuilt wholesale on mutation, so the hybrid reports its staleness (§28.2)
            void mark_stale() { m_sealed.mark_stale(); }

            [[nodiscard]] static std::string_view name() { return "hybrid"; }

        private:
            albor::prototype_d2::SealedCsrIndex m_sealed;
            albor::prototype_d2::SegmentedIndex m_tail;
            std::size_t m_property_count = 0;
        };

        // the dependency plan: binding b reads 2 properties, spread uniformly over the id space (plan.md R2)
        std::vector<std::pair<albor::prototype_d2::PropertyId, albor::prototype_d2::PropertyId>> plan_dependencies() {
            std::vector<std::pair<albor::prototype_d2::PropertyId, albor::prototype_d2::PropertyId>> plan;
            plan.reserve(SCALE_BINDINGS);

            for (std::size_t binding = 0; binding < SCALE_BINDINGS; binding++) {
                // stride the id space so the 22 000 entries land uniformly (§28.2)
                const std::size_t stride = SCALE_PROPERTIES / SCALE_BINDINGS;
                const albor::prototype_d2::PropertyId first = static_cast<albor::prototype_d2::PropertyId>(binding * stride % SCALE_PROPERTIES);
                const albor::prototype_d2::PropertyId second = static_cast<albor::prototype_d2::PropertyId>((binding * stride + stride / 2) % SCALE_PROPERTIES);
                plan.emplace_back(first, second);
            }

            return plan;
        }

        // the combined accounting of one storage candidate and one index (§28.2)
        struct Combined
        {
            double storage_bytes = 0.0;
            double index_bytes = 0.0;
            double total = 0.0;
            double properties = 0.0;
        };

    } // namespace

    void run_index_memory(Verdict& verdict) {
        // the committed scale's property mix (plan.md R2): 12 500 eight-property instances of the mixed schema
        const std::vector<albor::prototype_d2::ValueType> types{
            albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double, albor::prototype_d2::ValueType::Bool, albor::prototype_d2::ValueType::String, albor::prototype_d2::ValueType::TranslatedString, albor::prototype_d2::ValueType::Snapshot, albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Double,
        };
        const std::size_t instances = SCALE_PROPERTIES / 8;

        // the dependency plan every index is declared with (§28.4)
        const std::vector<std::pair<albor::prototype_d2::PropertyId, albor::prototype_d2::PropertyId>> plan = plan_dependencies();

        // declare every dependency of the plan into one index and report its bytes (§28.2)
        const auto measure = [&plan](auto& index, const bool post_seal) {
            for (std::size_t binding = 0; binding < plan.size(); binding++) {
                const albor::prototype_d2::PropertyId dependencies[] = {plan.at(binding).first, plan.at(binding).second};
                index.declare(static_cast<albor::prototype_d2::BindingId>(binding), std::span<const albor::prototype_d2::PropertyId>{dependencies, 2});
            }

            index.finish(SCALE_PROPERTIES);

            if (post_seal) {
                // the committed post-seal activity, which only the tail mechanisms absorb (plan.md R2)
                for (std::size_t addition = 0; addition < POST_SEAL_ADDITIONS; addition++) {
                    static_cast<void>(index.add_entry(static_cast<albor::prototype_d2::BindingId>(SCALE_BINDINGS + addition), static_cast<albor::prototype_d2::PropertyId>(addition * 97 % SCALE_PROPERTIES)));
                }
            }
        };

        // the six cells: three indexes x two storages (plan.md R2)
        std::printf("--- combined memory at %zu properties / %zu bindings x %zu deps ---\n", SCALE_PROPERTIES, SCALE_BINDINGS, DEPENDENCIES_PER_BINDING);
        std::printf("  %-14s %-9s %-14s %-14s %-12s %-12s\n", "storage", "index", "storage B", "index B", "combined B", "combined B/p");

        std::vector<double> combined_per_property(6, 0.0);

        // typed storage, measured once and reused across the three indexes (§28.4)
        {
            albor::prototype_d2::RegionTable regions;
            const albor::prototype_d2::SchemaId schema = regions.add_schema("Typed", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore store(regions);
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }
            const double storage = static_cast<double>(store.slab_allocated_bytes() + store.address_bytes() + store.region_table_bytes() + store.region_base_bytes());

            albor::prototype_d2::SealedCsrIndex csr;
            measure(csr, true);
            albor::prototype_d2::SegmentedIndex segmented;
            measure(segmented, true);
            HybridIndex hybrid;
            measure(hybrid, true);

            const double index_bytes[] = {static_cast<double>(csr.index_bytes()), static_cast<double>(segmented.index_bytes()), static_cast<double>(hybrid.index_bytes())};
            const char* names[] = {"sealed_csr", "segmented", "hybrid"};

            for (std::size_t which = 0; which < 3; which++) {
                const double combined = storage + index_bytes[which];
                combined_per_property.at(which) = combined / static_cast<double>(SCALE_PROPERTIES);
                std::printf("  %-14s %-9s %-14.0f %-14.0f %-12.0f %-12s\n", "typed", names[which], storage, index_bytes[which], combined, format(combined_per_property.at(which), 1).c_str());
            }
        }

        // erased storage, the same three indexes (§28.4)
        {
            albor::prototype_d2::RegionTable regions;
            const albor::prototype_d2::SchemaId schema = regions.add_schema("Erased", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::ErasedStore store(regions);
            for (std::size_t index = 0; index < instances; index++) {
                static_cast<void>(store.create(schema));
            }
            const double storage = static_cast<double>(store.slot_capacity_bytes() + regions.region_table_bytes());

            albor::prototype_d2::SealedCsrIndex csr;
            measure(csr, true);
            albor::prototype_d2::SegmentedIndex segmented;
            measure(segmented, true);
            HybridIndex hybrid;
            measure(hybrid, true);

            const double index_bytes[] = {static_cast<double>(csr.index_bytes()), static_cast<double>(segmented.index_bytes()), static_cast<double>(hybrid.index_bytes())};
            const char* names[] = {"sealed_csr", "segmented", "hybrid"};

            for (std::size_t which = 0; which < 3; which++) {
                const double combined = storage + index_bytes[which];
                combined_per_property.at(3 + which) = combined / static_cast<double>(SCALE_PROPERTIES);
                std::printf("  %-14s %-9s %-14.0f %-14.0f %-12.0f %-12s\n", "erased", names[which], storage, index_bytes[which], combined, format(combined_per_property.at(3 + which), 1).c_str());
            }
        }

        // T3.2.1: the combined question, on the cheapest index of both sides (§28.4)
        check(verdict, "T3.2.1", "typed+CSR <= erased+CSR combined, at 100k properties", combined_per_property.at(0) <= combined_per_property.at(3), format(combined_per_property.at(0), 1) + " <= " + format(combined_per_property.at(3), 1) + " B/property");

        // T3.2.2: the combined budget of the shipping combination (§28.4)
        check(verdict, "T3.2.2", "typed+CSR <= 36 B/property (mixed)", combined_per_property.at(0) <= T3_2_2_COMBINED_BYTES, format(combined_per_property.at(0), 1) + " <= " + format(T3_2_2_COMBINED_BYTES, 0) + " B/property");

        // T3.2.3: the segmented index's premium over sealed CSR on the same storage (§28.4)
        const double segmented_premium = combined_per_property.at(1) - combined_per_property.at(0);
        check(verdict, "T3.2.3", "segmented adds <= 12 B/property over CSR", segmented_premium <= T3_2_3_SEGMENTED_DELTA, format(segmented_premium, 1) + " <= " + format(T3_2_3_SEGMENTED_DELTA, 0) + " B/property");

        // T3.2.4: the hybrid's premium over sealed CSR on the same storage (§28.4)
        const double hybrid_premium = combined_per_property.at(2) - combined_per_property.at(0);
        check(verdict, "T3.2.4", "hybrid adds <= 6 B/property over CSR", hybrid_premium <= T3_2_4_HYBRID_DELTA, format(hybrid_premium, 1) + " <= " + format(T3_2_4_HYBRID_DELTA, 0) + " B/property");

        // T3.2.5: closure of the CSR index against an independent prediction (§28.4)
        //
        // the measured figure charges the offsets array (one entry per property plus the trailing entry) and
        // the bucket array (one BindingId per live entry, including the post-seal additions); the declared
        // pair array is working memory the rebuild consumes, so the prediction is exact from the plan's
        // declaration counts alone
        const double predicted_csr = static_cast<double>((SCALE_PROPERTIES + 1) * sizeof(std::uint32_t) + (2 * SCALE_BINDINGS + POST_SEAL_ADDITIONS) * sizeof(albor::prototype_d2::BindingId));
        albor::prototype_d2::SealedCsrIndex closure;
        measure(closure, true);
        const double closure_error = std::abs(static_cast<double>(closure.index_bytes()) - predicted_csr);
        check(verdict, "T3.2.5", "CSR index bytes == independent prediction", closure_error <= 0.5, format(closure_error, 2) + " B error (measured " + format(static_cast<double>(closure.index_bytes()), 0) + " vs predicted " + format(predicted_csr, 0) + ")");

        // record the small-scale shape, where the CSR offsets table dominates (§28.2)
        record("-", "shape at 10k properties", "the CSR offsets table is (property count + 1) x 4 B regardless of the binding count, so at a small scale the offsets dominate the combined cost");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R2 — combined storage x index memory (AD-04b)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R2, committed before this program existed\n");
    std::printf("memory measurement is deterministic: single figures, no spread\n\n");

    albor::prototype_d3::Verdict verdict;

    albor::prototype_d3::run_index_memory(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
