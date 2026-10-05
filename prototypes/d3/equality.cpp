// Prototype D3 R6 — equality semantics for the write-compare (F2.6)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// §13.3 requires a write that leaves the value equal not to invalidate a dependent, and does not say what equal
// means. The harness's own comparison is exact for scalars, payload identity for strings, and identity-then-
// content for snapshots. This revision measures what each candidate policy costs, what it suppresses, and
// whether it can leave a settle unable to converge

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        using albor::prototype_d2::EqualityPolicy;
        using albor::prototype_d2::Value;
        using albor::prototype_d2::ValueType;
        using albor::prototype_d2::WriteSource;

        // T3.6.3 D2 T1.5's original budget for one non-trivial property, unmodified
        constexpr double T3_6_3_STRING_NS = 200.0;

        // T3.6.4 a thousand snapshots published in one frame must stay inside a millisecond
        constexpr double T3_6_4_SNAPSHOT_NS = 1000.0;

        // T3.6.5 one invalidation per thousand equal writes at most
        constexpr double T3_6_5_CHURN_INVALIDATIONS = 1.0;

        // T3.6.6 a scalar compare must not be charged for the payload paths
        constexpr double T3_6_6_SCALAR_NS = 1.0;

        // the committed churn workload: 1000 writes of a value equal to the one already stored (§28.4)
        constexpr std::size_t CHURN_WRITES = 1000;

        // the committed realistic payloads: a 24-character string and a 1024-sample snapshot (§28.4)
        constexpr std::size_t STRING_LENGTH = 24;
        constexpr std::size_t SNAPSHOT_SAMPLES = 1024;

        // the committed comparison workload: 5 runs of 20000 comparisons per variant per run, in blocks of 2000
        constexpr std::size_t RUNS = 5;
        constexpr std::size_t COMPARISONS_PER_RUN = 20000;
        constexpr std::size_t BLOCK = 2000;

        // the accumulator every comparison loop folds into, so no comparison can be optimised away (§28.2)
        std::uint64_t g_sink = 0;

        const std::vector<EqualityPolicy> POLICIES{EqualityPolicy::Exact, EqualityPolicy::Ieee, EqualityPolicy::BitPattern, EqualityPolicy::Tolerant, EqualityPolicy::StringContent};

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, const bool passed, const std::string& detail) {
            std::printf("  %-8s %-52s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", detail.c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, const std::string& description, const std::string& detail) { std::printf("  %-8s %-52s %-4s %s\n", std::string(id).c_str(), description.c_str(), "REC", detail.c_str()); }

        std::string format(const double value, const int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // report the sample at a fraction, so a latency row is comparable across policies (§28.2)
        double percentile(std::vector<double> samples, const double fraction) {
            // report nothing when the variant measured no sample (§28.2)
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

        double now_ns() { return std::chrono::duration<double, std::nano>(Clock::now().time_since_epoch()).count(); }

        // build the string payload the corpus-sized comparison uses (§28.4)
        Value make_text(const std::string& text) { return Value::make_string(text); }

        // build the snapshot payload the corpus-sized comparison uses, which is immutable and shared (§10.5)
        Value make_samples(const double seed) {
            std::vector<double> samples(SNAPSHOT_SAMPLES);

            // fill the payload with a deterministic ramp, so two payloads can be equal without being the same object
            for (std::size_t index = 0; index < samples.size(); index++) {
                samples.at(index) = seed + static_cast<double>(index);
            }

            // publish it as an immutable snapshot (§13.2)
            return Value::make_snapshot(std::make_shared<const std::vector<double>>(samples));
        }

        // hold one policy's two equal-but-distinct payloads per payload class (§28.2)
        struct Payloads
        {
            Value scalar_left = Value(static_cast<double>(1.5));
            Value scalar_right = Value(static_cast<double>(1.5));
            Value string_left = make_text(std::string(STRING_LENGTH, 'a'));
            Value string_right = make_text(std::string(STRING_LENGTH, 'a'));
            Value snapshot_left = make_samples(0.0);
            Value snapshot_right = make_samples(0.0);
        };

        // hold one variant's block timings and its per-run medians (§28.2)
        struct Variant
        {
            std::string name;
            std::vector<double> samples;
            std::vector<double> run_p50;
        };

        // report a variant's fastest run per comparison, which interference can only slow down (§28.2)
        double fastest_of(const Variant& variant) {
            // report nothing when the variant recorded no run (§28.2)
            if (variant.run_p50.empty()) {
                return 0.0;
            }

            // hand back the fastest run's per-comparison cost (§28.2)
            return *std::ranges::min_element(variant.run_p50.begin(), variant.run_p50.end()) / static_cast<double>(BLOCK);
        }

        // report a variant's p99 per comparison, in nanoseconds (§28.2)
        double p99_of(const Variant& variant) { return percentile(variant.samples, 0.99) / static_cast<double>(BLOCK); }

        // measure every policy against every payload class, interleaved in blocks so drift hits them equally (§28.4)
        std::vector<Variant> measure_costs() {
            const Payloads payloads;

            // one variant per policy and payload class, so a comparison class is never measured in isolation (§28.4)
            std::vector<Variant> variants;
            variants.reserve(POLICIES.size() * 3);

            for (const EqualityPolicy policy : POLICIES) {
                variants.push_back(Variant{std::string(albor::prototype_d2::policy_name(policy)) + " scalar", {}, {}});
                variants.push_back(Variant{std::string(albor::prototype_d2::policy_name(policy)) + " string", {}, {}});
                variants.push_back(Variant{std::string(albor::prototype_d2::policy_name(policy)) + " snapshot", {}, {}});
            }

            // run the variants in blocks, so every policy sees the same machine state (§28.4)
            for (std::size_t run = 0; run < RUNS; run++) {
                for (std::size_t block = 0; block < COMPARISONS_PER_RUN; block += BLOCK) {
                    for (std::size_t which = 0; which < POLICIES.size(); which++) {
                        const EqualityPolicy policy = POLICIES.at(which);

                        // the scalar class, where every policy differs only in its double rule (§13.1)
                        {
                            const double start = now_ns();

                            for (std::size_t iteration = 0; iteration < BLOCK; iteration++) {
                                g_sink += payloads.scalar_left.equal_under(payloads.scalar_right, policy) ? 1u : 0u;
                            }

                            variants.at(which * 3).samples.push_back(now_ns() - start);
                        }

                        // the string class, where two equal contents sit behind two distinct payloads (§13.1)
                        {
                            const double start = now_ns();

                            for (std::size_t iteration = 0; iteration < BLOCK; iteration++) {
                                g_sink += payloads.string_left.equal_under(payloads.string_right, policy) ? 1u : 0u;
                            }

                            variants.at(which * 3 + 1).samples.push_back(now_ns() - start);
                        }

                        // the snapshot class, where two equal payloads sit behind two distinct boxes (§10.5)
                        {
                            const double start = now_ns();

                            for (std::size_t iteration = 0; iteration < BLOCK; iteration++) {
                                g_sink += payloads.snapshot_left.equal_under(payloads.snapshot_right, policy) ? 1u : 0u;
                            }

                            variants.at(which * 3 + 2).samples.push_back(now_ns() - start);
                        }
                    }
                }

                // record every variant's median block for this run (§28.2)
                for (Variant& variant : variants) {
                    const std::size_t first_block = variant.samples.size() - (COMPARISONS_PER_RUN / BLOCK);
                    std::vector<double> block_medians(variant.samples.begin() + static_cast<std::ptrdiff_t>(first_block), variant.samples.end());
                    variant.run_p50.push_back(percentile(std::move(block_medians), 0.50));
                }
            }

            // hand back every variant's timings (§28.2)
            return variants;
        }

        // the six semantic checks one policy is judged against (§28.4)
        struct Observables
        {
            bool equal_write_quiet = false;
            bool changed_write_loud = false;
            bool string_content_change = false;
            bool locale_change = false;
            bool equal_snapshot_quiet = false;
            bool changed_snapshot_loud = false;

            [[nodiscard]] bool all() const { return equal_write_quiet && changed_write_loud && string_content_change && locale_change && equal_snapshot_quiet && changed_snapshot_loud; }
        };

        // hold a store with one mixed-schema property of every payload class (§13.1)
        struct Fixture
        {
            albor::prototype_d2::RegionTable regions;
            albor::prototype_d2::TypedStore store;
            albor::prototype_d2::SchemaId schema = 0;
            albor::prototype_d2::InstanceId instance = 0;

            Fixture() :
                store(regions) {
                // one instance of the mixed schema, whose local indices are the six payload classes (§13.1)
                const std::vector<ValueType> types{ValueType::Int, ValueType::Double, ValueType::String, ValueType::TranslatedString, ValueType::Snapshot, ValueType::Bool};
                schema = regions.add_schema("Equality", albor::prototype_d2::InstanceKind::Component, types);
                instance = store.create(schema);
                store.set_equality_policy(EqualityPolicy::Exact);
            }

            [[nodiscard]] albor::prototype_d2::PropertyId at(const std::size_t local) const { return regions.global(instance, static_cast<albor::prototype_d2::LocalPropertyId>(local)); }
        };

        // judge one policy against the six semantic checks (§28.4)
        Observables check_observables(const EqualityPolicy policy) {
            Observables result;

            // each check gets its own store, because a check's writes would otherwise be the next check's starting value
            {
                Fixture fixture;
                fixture.store.set_equality_policy(policy);
                static_cast<void>(fixture.store.write(fixture.at(0), Value(static_cast<std::int64_t>(5)), WriteSource::Binding));
                result.equal_write_quiet = !fixture.store.write(fixture.at(0), Value(static_cast<std::int64_t>(5)), WriteSource::Binding);
                result.changed_write_loud = fixture.store.write(fixture.at(0), Value(static_cast<std::int64_t>(6)), WriteSource::Binding);
            }

            {
                Fixture fixture;
                fixture.store.set_equality_policy(policy);
                static_cast<void>(fixture.store.write(fixture.at(2), make_text(std::string(STRING_LENGTH, 'a')), WriteSource::Binding));
                result.string_content_change = fixture.store.write(fixture.at(2), make_text(std::string(STRING_LENGTH, 'b')), WriteSource::Binding);
            }

            {
                Fixture fixture;
                fixture.store.set_equality_policy(policy);
                static_cast<void>(fixture.store.write(fixture.at(3), Value::make_translated("hello", 1), WriteSource::Binding));
                result.locale_change = fixture.store.write(fixture.at(3), Value::make_translated("hello", 2), WriteSource::Binding);
            }

            {
                Fixture fixture;
                fixture.store.set_equality_policy(policy);
                static_cast<void>(fixture.store.write(fixture.at(4), make_samples(0.0), WriteSource::Binding));
                result.equal_snapshot_quiet = !fixture.store.write(fixture.at(4), make_samples(0.0), WriteSource::Binding);
                result.changed_snapshot_loud = fixture.store.write(fixture.at(4), make_samples(1.0), WriteSource::Binding);
            }

            // hand back what this policy passes (§28.2)
            return result;
        }

        // hold what one churn workload produced (§28.2)
        struct ChurnResult
        {
            std::size_t invalidations = 0;
            std::size_t evaluations = 0;
        };

        // build the value a churn write publishes, so a payload class can write a fresh but equal payload each time
        //
        // this matters because a payload written twice from one shared object is the same pointer, which every
        // policy reports as equal: the case the write-compare has to get right is a fresh payload of equal content
        using FreshEqual = Value (*)(std::size_t);

        Value fresh_scalar(const std::size_t) { return Value(1.5); }

        Value fresh_string(const std::size_t) { return make_text(std::string(STRING_LENGTH, 'a')); }

        Value fresh_snapshot(const std::size_t) { return make_samples(0.0); }

        // write one equal value many times and count what the write-compare let through (§13.3)
        template <class Store>
        ChurnResult measure_churn(albor::prototype_d2::Scene<Store>& scene, const EqualityPolicy policy, FreshEqual fresh, const std::size_t local) {
            // apply the policy to the store the scene writes through (§28.4)
            scene.store().set_equality_policy(policy);

            // the property the churn is written to (§13.3)
            const albor::prototype_d2::PropertyId property = scene.at(static_cast<albor::prototype_d2::InstanceId>(0), static_cast<albor::prototype_d2::LocalPropertyId>(local));

            // seed the property so every later write is equal to what is stored (§13.4)
            static_cast<void>(scene.store().write(property, fresh(0), WriteSource::GeneratedInit));

            // count from here, so the seeding write is not counted (§28.2)
            scene.graph().reset_invalidated_total();

            // write a fresh equal payload the committed number of times, which is what a poll of an observable does (§13.2)
            for (std::size_t write = 0; write < CHURN_WRITES; write++) {
                scene.graph().assign(property, fresh(write));
            }

            static_cast<void>(scene.graph().settle());

            // hand back what the policy let through (§28.2)
            return ChurnResult{scene.graph().invalidated_total(), 0};
        }

        // the binding that divides its dependency by zero, so a zero dependency produces NaN (§13.1)
        template <class Store>
        void divide_by_zero(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_double(context.read_double(context.declared().front()) / 0.0);
        }

        // the binding that publishes its dependency unchanged, so a NaN travels down the chain (§13.3)
        template <class Store>
        void pass_double(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_double(context.read_double(context.declared().front()));
        }

        // hold what one convergence run produced (§28.2)
        struct ConvergenceResult
        {
            std::size_t passes = 0;
            std::size_t second_drive_evaluations = 0;
            bool converged = false;
            bool budget_exhausted = false;
            double final_value = 0.0;
        };

        // build the committed NaN chain and report what the policy let happen (§28.4)
        template <class Store>
        ConvergenceResult measure_convergence(const EqualityPolicy policy) {
            albor::prototype_d2::Scene<Store> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            scene.store().set_equality_policy(policy);

            const albor::prototype_d2::SchemaId schema = scene.regions().add_schema("NaN", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Double});

            // three links, each bound to the one before it, with the first dividing by zero (§28.4)
            std::vector<albor::prototype_d2::PropertyId> chain;

            for (std::size_t link = 0; link < 3; link++) {
                const albor::prototype_d2::InstanceId instance = scene.store().create(schema);
                chain.push_back(scene.at(instance, 0));
                static_cast<void>(scene.store().write(chain.back(), Value(0.0), WriteSource::GeneratedInit));
            }

            static_cast<void>(scene.graph().add_binding(chain.at(1), std::vector<albor::prototype_d2::PropertyId>{chain.at(0)}, &divide_by_zero<Store>));
            static_cast<void>(scene.graph().add_binding(chain.at(2), std::vector<albor::prototype_d2::PropertyId>{chain.at(1)}, &pass_double<Store>));

            // seal and settle once, which is where the NaN is produced (§13.4)
            scene.graph().seal();
            const albor::prototype_d2::SettleStats first = scene.graph().settle();

            // drive the source with negative zero, which is a different representation of the same value and still
            // produces NaN, so the drive has to re-enter the chain under any policy but the one that calls them equal
            scene.graph().assign(chain.at(0), Value(-0.0));
            const albor::prototype_d2::SettleStats second = scene.graph().settle();

            ConvergenceResult result;
            result.passes = first.passes + second.passes;
            result.second_drive_evaluations = second.bindings_evaluated;
            result.budget_exhausted = first.cycle_budget_exhausted || second.cycle_budget_exhausted;
            result.final_value = scene.store().read(chain.at(2)).as_double();

            // a settle converged when it reached a fixpoint inside the 64-pass guard, which is what the committed
            // threshold names; the second drive's evaluation count is recorded beside it because a policy that
            // re-evaluates on an equal representation has cost the frame time even when it converged (§28.4)
            result.converged = !result.budget_exhausted;
            return result;
        }

        // pick the policy T3.6.2 judges: among the converging policies, the cheapest at the realistic payloads (§28.4)
        std::size_t choose_recommended(const std::vector<bool>& converges, const std::vector<Variant>& variants) {
            std::size_t recommended = 0;
            double best = 0.0;

            for (std::size_t which = 0; which < POLICIES.size(); which++) {
                // a policy that cannot converge is never the default, whatever it costs (§28.2)
                if (!converges.at(which)) {
                    continue;
                }

                // the realistic payload is the snapshot, so its cost decides between the converging policies (§28.4)
                const double cost = fastest_of(variants.at(which * 3 + 2));

                if (recommended == 0 || cost < best) {
                    recommended = which;
                    best = cost;
                }
            }

            // report the chosen policy, or 0 when none converged (§28.2)
            return recommended;
        }

    } // namespace

    void run_equality(Verdict& verdict) {
        std::printf("--- the five policies, and what each one costs (ns per comparison, fastest of %zu runs) ---\n", RUNS);
        std::printf("  %-18s %12s %12s %12s\n", "policy", "scalar", "string", "snapshot");

        const std::vector<Variant> variants = measure_costs();

        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            std::printf("  %-18s %12s %12s %12s\n", std::string(albor::prototype_d2::policy_name(POLICIES.at(which))).c_str(), format(fastest_of(variants.at(which * 3)), 3).c_str(), format(fastest_of(variants.at(which * 3 + 1)), 2).c_str(), format(fastest_of(variants.at(which * 3 + 2)), 2).c_str());
        }

        // judge each policy against the six semantic checks (§28.4)
        std::vector<Observables> observables;
        observables.reserve(POLICIES.size());

        for (const EqualityPolicy policy : POLICIES) {
            observables.push_back(check_observables(policy));
        }

        std::printf("--- the six semantic checks, per policy ---\n");
        std::printf("  %-18s %8s %8s %8s %8s %8s %8s\n", "policy", "a quiet", "b loud", "c string", "d locale", "e snapshot", "f changed");

        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            const Observables& result = observables.at(which);
            const auto mark = [](const bool passed) { return passed ? "pass" : "FAIL"; };
            std::printf("  %-18s %8s %8s %8s %8s %8s %8s\n", std::string(albor::prototype_d2::policy_name(POLICIES.at(which))).c_str(), mark(result.equal_write_quiet), mark(result.changed_write_loud), mark(result.string_content_change), mark(result.locale_change), mark(result.equal_snapshot_quiet), mark(result.changed_snapshot_loud));
        }

        // measure the churn each policy lets through, on a scalar and on a payload (§28.4)
        std::vector<ChurnResult> scalar_churn;
        std::vector<ChurnResult> string_churn;
        std::vector<ChurnResult> snapshot_churn;
        std::vector<ConvergenceResult> convergence;

        for (const EqualityPolicy policy : POLICIES) {
            {
                albor::prototype_d2::Scene<albor::prototype_d2::TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
                const albor::prototype_d2::SchemaId schema = scene.regions().add_schema("Churn", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Double, ValueType::String, ValueType::Snapshot});
                static_cast<void>(scene.store().create(schema));
                scalar_churn.push_back(measure_churn(scene, policy, &fresh_scalar, 0));
            }

            {
                albor::prototype_d2::Scene<albor::prototype_d2::TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
                const albor::prototype_d2::SchemaId schema = scene.regions().add_schema("Churn", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Double, ValueType::String, ValueType::Snapshot});
                static_cast<void>(scene.store().create(schema));
                string_churn.push_back(measure_churn(scene, policy, &fresh_string, 1));
            }

            {
                albor::prototype_d2::Scene<albor::prototype_d2::TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
                const albor::prototype_d2::SchemaId schema = scene.regions().add_schema("Churn", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Double, ValueType::String, ValueType::Snapshot});
                static_cast<void>(scene.store().create(schema));
                snapshot_churn.push_back(measure_churn(scene, policy, &fresh_snapshot, 2));
            }

            convergence.push_back(measure_convergence<albor::prototype_d2::TypedStore>(policy));
        }

        std::printf("--- churn: invalidations from %zu writes of an equal value ---\n", CHURN_WRITES);
        std::printf("  %-18s %12s %12s %12s\n", "policy", "scalar", "string", "snapshot");

        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            std::printf("  %-18s %12zu %12zu %12zu\n", std::string(albor::prototype_d2::policy_name(POLICIES.at(which))).c_str(), scalar_churn.at(which).invalidations, string_churn.at(which).invalidations, snapshot_churn.at(which).invalidations);
        }

        // T3.6.1: at least one policy has to converge the NaN chain, and the others have to be named (§28.4)
        std::vector<bool> converges;
        std::string converging_names;
        std::string failing_names;

        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            converges.push_back(convergence.at(which).converged);

            const std::string name = std::string(albor::prototype_d2::policy_name(POLICIES.at(which)));

            if (convergence.at(which).converged) {
                converging_names += (converging_names.empty() ? "" : ", ") + name;
            }
            else {
                failing_names += (failing_names.empty() ? "" : ", ") + name;
            }
        }

        check(verdict, "T3.6.1", "at least one policy converges the NaN chain", !converging_names.empty(), converging_names.empty() ? ("none converged: " + failing_names) : ("converged: " + converging_names));

        // T3.6.2: the converging policy that is cheapest at the realistic payload has to pass all six checks (§28.4)
        const std::size_t recommended = choose_recommended(converges, variants);
        const Observables& recommended_observables = observables.at(recommended);
        const auto mark_check = [](const bool passed) { return passed ? std::string("pass") : std::string("FAIL"); };
        check(verdict, "T3.6.2", "the recommended policy passes all six observables", recommended_observables.all(), std::string(albor::prototype_d2::policy_name(POLICIES.at(recommended))) + ": a=" + mark_check(recommended_observables.equal_write_quiet) + " b=" + mark_check(recommended_observables.changed_write_loud) + " c=" + mark_check(recommended_observables.string_content_change) + " d=" + mark_check(recommended_observables.locale_change) + " e=" + mark_check(recommended_observables.equal_snapshot_quiet) + " f=" + mark_check(recommended_observables.changed_snapshot_loud));

        // the index of the one policy that compares string and snapshot content, which is what the two budgets name
        const std::size_t content_policy = static_cast<std::size_t>(std::ranges::find(POLICIES, EqualityPolicy::StringContent) - POLICIES.begin());

        // T3.6.3: the string content comparison against D2 T1.5's original budget (§28.4)
        const double string_cost = p99_of(variants.at(content_policy * 3 + 1));
        check(verdict, "T3.6.3", "24-character string content comparison p99 <= 200 ns", string_cost <= T3_6_3_STRING_NS, format(string_cost, 2) + " ns <= " + format(T3_6_3_STRING_NS, 0) + " ns (string_content policy)");

        // T3.6.4: the snapshot content comparison, so a frame of equal publishes stays inside a millisecond (§28.4)
        const double snapshot_cost = p99_of(variants.at(content_policy * 3 + 2));
        check(verdict, "T3.6.4", "1024-sample snapshot content comparison p99 <= 1000 ns", snapshot_cost <= T3_6_4_SNAPSHOT_NS, format(snapshot_cost, 2) + " ns <= " + format(T3_6_4_SNAPSHOT_NS, 0) + " ns (string_content policy)");

        // T3.6.5: the recommended policy has to keep an equal write off the invalidation path (§28.4)
        const double worst_churn = static_cast<double>(std::max({scalar_churn.at(recommended).invalidations, string_churn.at(recommended).invalidations, snapshot_churn.at(recommended).invalidations}));
        check(verdict, "T3.6.5", "recommended policy: <= 1 invalidation per 1000 equal writes", worst_churn <= T3_6_5_CHURN_INVALIDATIONS, std::to_string(scalar_churn.at(recommended).invalidations) + " scalar, " + std::to_string(string_churn.at(recommended).invalidations) + " string, " + std::to_string(snapshot_churn.at(recommended).invalidations) + " snapshot");

        // T3.6.6: a scalar compare must not be charged for the payload paths (§28.4)
        const double scalar_delta = fastest_of(variants.at(recommended * 3)) - fastest_of(variants.at(0));
        check(verdict, "T3.6.6", "recommended policy costs <= 1 ns more than exact on a scalar", scalar_delta <= T3_6_6_SCALAR_NS, format(scalar_delta, 3) + " ns <= " + format(T3_6_6_SCALAR_NS, 0) + " ns");

        // record the per-policy convergence and cost detail, which is the decision input (§28.2)
        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            record("-", std::string("convergence: ") + std::string(albor::prototype_d2::policy_name(POLICIES.at(which))), std::to_string(convergence.at(which).passes) + " passes, converged=" + (convergence.at(which).converged ? "yes" : "no") + ", guard exhausted=" + (convergence.at(which).budget_exhausted ? "yes" : "no") + ", second drive re-evaluated " + std::to_string(convergence.at(which).second_drive_evaluations) + " bindings, final=" + format(convergence.at(which).final_value, 1));
        }

        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            record("-", std::string("p99 cost: ") + std::string(albor::prototype_d2::policy_name(POLICIES.at(which))), format(p99_of(variants.at(which * 3)), 3) + " ns scalar, " + format(p99_of(variants.at(which * 3 + 1)), 2) + " ns string, " + format(p99_of(variants.at(which * 3 + 2)), 2) + " ns snapshot");
        }

        // record what the committed chain cannot show on its own: how each policy reads the two representations a
        // double can take that a binding cannot produce, which is where equality semantics differ most visibly
        for (std::size_t which = 0; which < POLICIES.size(); which++) {
            const EqualityPolicy policy = POLICIES.at(which);
            const bool zero_pair_equal = Value(0.0).equal_under(Value(-0.0), policy);
            const bool nan_pair_equal = Value(std::nan("")).equal_under(Value(-std::nan("")), policy);
            record("-", std::string("double edges: ") + std::string(albor::prototype_d2::policy_name(policy)), std::string("0.0 == -0.0 is ") + (zero_pair_equal ? "equal" : "a change") + ", NaN == -NaN is " + (nan_pair_equal ? "equal" : "a change"));
        }

        // record the cheapest policy that does pass every observable, which is the viable default the failing
        // T3.6.2 cannot name on its own (§28.4)
        {
            std::size_t viable = 0;
            double best = 0.0;

            for (std::size_t which = 0; which < POLICIES.size(); which++) {
                if (!observables.at(which).all()) {
                    continue;
                }

                const double cost = fastest_of(variants.at(which * 3 + 2));

                if (viable == 0 || cost < best) {
                    viable = which;
                    best = cost;
                }
            }

            if (viable > 0) {
                record("-", "cheapest policy passing all six", std::string(albor::prototype_d2::policy_name(POLICIES.at(viable))) + ", snapshot compare " + format(fastest_of(variants.at(viable * 3 + 2)), 1) + " ns, scalar " + format(fastest_of(variants.at(viable * 3)), 2) + " ns, string " + format(fastest_of(variants.at(viable * 3 + 1)), 1) + " ns");
            }
        }

        record("-", "comparison accumulator", std::to_string(g_sink));
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R6 — equality semantics for the write-compare (AD-04b, §13.3)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R6, committed before this program existed\n");
    std::printf("the default policy is exact, which is the comparison D2 recorded, so D2's figures stay reproducible\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_equality(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
