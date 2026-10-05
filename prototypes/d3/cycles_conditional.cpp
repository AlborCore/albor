// Prototype D3 R9 — static cycle rejection with conditional dependencies (F2.9)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// §7 and AD-04c propose promoting Kahn cycle detection from a runtime guard to a compile-time rejection. A
// dependency added at runtime means the runtime graph is not the declared graph, so this revision asks when
// a static rejection is still sound, what a runtime guard costs after a mutation, and what it costs to keep
// the static check sound by declaring the union of the dependencies a binding might ever have

#include "prototypes/d2/src/cycles.h"
#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        using albor::prototype_d2::BindingId;
        using albor::prototype_d2::BindingFn;
        using albor::prototype_d2::InstanceId;
        using albor::prototype_d2::PropertyId;
        using albor::prototype_d2::Scene;
        using albor::prototype_d2::SchemaId;
        using albor::prototype_d2::TypedStore;
        using albor::prototype_d2::Value;
        using albor::prototype_d2::ValueType;
        using albor::prototype_d2::WriteSource;

        // T3.9.3 what a runtime guard costs after one conditional edge, which decides whether it is affordable
        constexpr double T3_9_3_DETECTION_MS = 2.0;

        // T3.9.5 over-declaration is the price of static soundness, and it has a bound
        constexpr double T3_9_5_INVALIDATION_RATIO = 2.0;
        constexpr double T3_9_5_INDEX_GROWTH = 0.25;

        // the committed scale for the detection cost, the same one R2 and R8 account at (§28.4)
        constexpr std::size_t DETECTION_BINDINGS = 11000;

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, const std::string& description, const bool passed, const std::string& detail) {
            std::printf("  %-8s %-52s %-4s %s\n", std::string(id).c_str(), description.c_str(), passed ? "PASS" : "FAIL", detail.c_str());

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

        using Clock = std::chrono::steady_clock;

        double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

        // declare the one scalar component every topology is built from (§13.1)
        SchemaId declare(Scene<TypedStore>& scene) { return scene.regions().add_schema("Scalar", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int}); }

        // hold one declared topology and the oracle that says whether it is cyclic (§28.4)
        struct Topology
        {
            std::string name;
            bool cyclic = false;
        };

        // build the committed corpus's seven acyclic topologies and judge each twice (§28.4)
        template <class Store>
        std::string build_acyclic_chain(Scene<Store>& scene, const SchemaId schema, const std::size_t depth) {
            const std::vector<PropertyId> chain = albor::prototype_d2::build_chain(scene, schema, depth);
            static_cast<void>(chain);
            return "chain of depth " + std::to_string(depth);
        }

        // hold the result of judging one topology (§28.2)
        struct Judgement
        {
            std::string name;
            bool cyclic = false;
            bool detected_cycle = false;
            bool order_proved_acyclic = false;
            bool budget_exhausted = false;
        };

        // judge one topology: Kahn over the binding graph, and whether the sealed order placed every binding (§28.4)
        template <class Store>
        Judgement judge(Scene<Store>& scene) {
            Judgement result;
            scene.graph().seal();

            // run Kahn over the declared graph, which is the check the compiler would run (§7 T7.1)
            const albor::prototype_d2::CycleReport report = albor::prototype_d2::detect_cycles(scene.graph());
            result.detected_cycle = report.has_cycle;

            // the sealed order is the second opinion: Kahn reaching every binding is exactly acyclicity (§7)
            result.order_proved_acyclic = albor::prototype_d2::seal_proved_acyclic(scene.graph());

            // settle it, so the pass guard's verdict on an acyclic shape is recorded rather than assumed (§28.2)
            const albor::prototype_d2::SettleStats stats = scene.graph().settle();
            result.budget_exhausted = stats.cycle_budget_exhausted;
            return result;
        }

        // build every cyclic topology the corpus commits, each with its oracle (§28.4)
        template <class Store>
        Judgement build_self_loop(Scene<Store>& scene, const SchemaId schema, const bool cyclic) {
            // one property whose binding reads the property it writes, which is the smallest cycle there is (§7)
            const InstanceId instance = scene.store().create(schema);
            const PropertyId property = scene.at(instance, 0);
            static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
            static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{property}, &albor::prototype_d2::increment_first<Store>));

            Judgement result = judge(scene);
            result.name = "self_loop";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_two_cycle(Scene<Store>& scene, const SchemaId schema, const bool cyclic) {
            // two properties whose bindings read each other, which is the cycle a two-node expression makes (§7)
            const InstanceId first = scene.store().create(schema);
            const InstanceId second = scene.store().create(schema);
            const PropertyId a = scene.at(first, 0);
            const PropertyId b = scene.at(second, 0);
            static_cast<void>(scene.store().write(a, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            static_cast<void>(scene.store().write(b, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            static_cast<void>(scene.graph().add_binding(b, std::vector<PropertyId>{a}, &albor::prototype_d2::increment_first<Store>));
            static_cast<void>(scene.graph().add_binding(a, std::vector<PropertyId>{b}, &albor::prototype_d2::increment_first<Store>));

            Judgement result = judge(scene);
            result.name = "two_cycle";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_three_cycle(Scene<Store>& scene, const SchemaId schema, const bool cyclic) {
            // three properties in a ring, the shape a formatting cycle takes (§7)
            std::vector<PropertyId> ring;

            for (std::size_t index = 0; index < 3; index++) {
                const InstanceId instance = scene.store().create(schema);
                ring.push_back(scene.at(instance, 0));
                static_cast<void>(scene.store().write(ring.back(), Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            }

            for (std::size_t index = 0; index < 3; index++) {
                static_cast<void>(scene.graph().add_binding(ring.at(index), std::vector<PropertyId>{ring.at((index + 1) % 3)}, &albor::prototype_d2::increment_first<Store>));
            }

            Judgement result = judge(scene);
            result.name = "three_cycle";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_cycle_behind_fan_in(Scene<Store>& scene, const SchemaId schema, const bool cyclic) {
            // an acyclic fan-in in front of a cycle, so the rejection has to see past the acyclic part (§7)
            const InstanceId source_instance = scene.store().create(schema);
            const PropertyId source = scene.at(source_instance, 0);
            static_cast<void>(scene.store().write(source, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));

            std::vector<PropertyId> middle;

            for (std::size_t index = 0; index < 3; index++) {
                const InstanceId instance = scene.store().create(schema);
                middle.push_back(scene.at(instance, 0));
                static_cast<void>(scene.store().write(middle.back(), Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
                static_cast<void>(scene.graph().add_binding(middle.back(), std::vector<PropertyId>{source}, &albor::prototype_d2::pass_int<Store>));
            }

            // the ring behind the fan-in (§7)
            for (std::size_t index = 0; index < 3; index++) {
                static_cast<void>(scene.graph().add_binding(middle.at(index), std::vector<PropertyId>{middle.at((index + 1) % 3)}, &albor::prototype_d2::increment_first<Store>));
            }

            Judgement result = judge(scene);
            result.name = "cycle_behind_fan_in";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_acyclic_tree(Scene<Store>& scene, const SchemaId schema, const bool cyclic, const std::size_t leaves) {
            // a tree of the given leaf count, which is what a wide real graph looks like (§7)
            const InstanceId root_instance = scene.store().create(schema);
            const PropertyId root = scene.at(root_instance, 0);
            static_cast<void>(scene.store().write(root, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));

            for (std::size_t leaf = 0; leaf < leaves; leaf++) {
                const InstanceId instance = scene.store().create(schema);
                const PropertyId property = scene.at(instance, 0);
                static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{root}, &albor::prototype_d2::pass_int<Store>));
            }

            Judgement result = judge(scene);
            result.name = "acyclic_tree_" + std::to_string(leaves) + "_leaves";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_unbound_source_inside(Scene<Store>& scene, const SchemaId schema, const bool cyclic) {
            // a reads b and an unbound property; b reads only the unbound property, so b orders a but not the
            // other way round. A checker that treated every declared name as an ordering edge would call this a cycle
            const InstanceId first = scene.store().create(schema);
            const InstanceId second = scene.store().create(schema);
            const InstanceId middle = scene.store().create(schema);
            const PropertyId a = scene.at(first, 0);
            const PropertyId b = scene.at(second, 0);
            const PropertyId unbound = scene.at(middle, 0);

            static_cast<void>(scene.store().write(a, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            static_cast<void>(scene.store().write(b, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            static_cast<void>(scene.store().write(unbound, Value(static_cast<std::int64_t>(1)), WriteSource::Imperative));

            // b reads the unbound property only, and a reads b plus the same unbound property (§7)
            static_cast<void>(scene.graph().add_binding(b, std::vector<PropertyId>{unbound}, &albor::prototype_d2::pass_int<Store>));
            static_cast<void>(scene.graph().add_binding(a, std::vector<PropertyId>{b, unbound}, &albor::prototype_d2::offset_then_clamp<Store>));

            Judgement result = judge(scene);
            result.name = "unbound_dependency_is_not_an_edge";
            result.cyclic = cyclic;
            return result;
        }

        template <class Store>
        Judgement build_acyclic(Scene<Store>& scene, const SchemaId schema, const std::string& name, const std::size_t shape) {
            // the four committed acyclic shapes, each built by the D2 harness so the topologies are its own (§28.4)
            if (name == "chain16") {
                static_cast<void>(build_acyclic_chain(scene, schema, 16));
            }
            else if (name == "diamond32") {
                static_cast<void>(albor::prototype_d2::build_diamond(scene, schema, 32));
            }
            else if (name == "reconvergent") {
                static_cast<void>(albor::prototype_d2::build_reconvergent(scene, schema, 8, 8));
            }
            else {
                static_cast<void>(albor::prototype_d2::build_d_shape(scene, schema, 16, 8));
            }

            Judgement result = judge(scene);
            result.name = name;
            result.cyclic = false;
            static_cast<void>(shape);
            return result;
        }

        // build the committed conditional cycle: an acyclic graph, then one runtime edge that closes a ring (§28.4)
        struct ConditionalCycle
        {
            PropertyId source = 0;
            PropertyId first = 0;
            PropertyId second = 0;
            std::size_t bindings_before = 0;
        };

        template <class Store>
        ConditionalCycle build_conditional(Scene<Store>& scene, const SchemaId schema) {
            // a two-link chain, which is acyclic as declared: the second link increments the first (§18.1)
            const InstanceId source_instance = scene.store().create(schema);
            const InstanceId first_instance = scene.store().create(schema);
            const InstanceId second_instance = scene.store().create(schema);
            const PropertyId source = scene.at(source_instance, 0);
            const PropertyId first = scene.at(first_instance, 0);
            const PropertyId second = scene.at(second_instance, 0);

            static_cast<void>(scene.store().write(source, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            static_cast<void>(scene.store().write(first, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
            static_cast<void>(scene.store().write(second, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
            static_cast<void>(scene.graph().add_binding(first, std::vector<PropertyId>{source}, &albor::prototype_d2::sum_all<Store>));
            static_cast<void>(scene.graph().add_binding(second, std::vector<PropertyId>{first}, &albor::prototype_d2::increment_first<Store>));
            scene.graph().seal();
            static_cast<void>(scene.graph().settle());

            ConditionalCycle result;
            result.source = source;
            result.first = first;
            result.second = second;
            result.bindings_before = scene.graph().binding_count();
            return result;
        }

    } // namespace

    void run_cycles(Verdict& verdict) {
        std::vector<Judgement> judgements;

        // the five cyclic topologies the corpus commits (§28.4)
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_self_loop(scene, declare(scene), true));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_two_cycle(scene, declare(scene), true));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_three_cycle(scene, declare(scene), true));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_cycle_behind_fan_in(scene, declare(scene), true));
        }

        // the seven acyclic topologies the corpus commits (§28.4)
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_acyclic(scene, declare(scene), "chain16", 0));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_acyclic(scene, declare(scene), "diamond32", 0));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_acyclic(scene, declare(scene), "reconvergent", 0));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_acyclic(scene, declare(scene), "d_shape", 0));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_acyclic_tree(scene, declare(scene), false, 1000));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            judgements.push_back(build_unbound_source_inside(scene, declare(scene), false));
        }
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::PullTopo, false);
            judgements.push_back(build_acyclic(scene, declare(scene), "chain16_pull_model", 0));
        }

        std::printf("--- the committed corpus, judged by Kahn and by the sealed order ---\n");
        std::printf("  %-34s %8s %10s %14s %10s\n", "topology", "cyclic", "Kahn finds", "order acyclic", "guard hit");

        for (const Judgement& judgement : judgements) {
            std::printf("  %-34s %8s %10s %14s %10s\n", judgement.name.c_str(), judgement.cyclic ? "yes" : "no", judgement.detected_cycle ? "cycle" : "none", judgement.order_proved_acyclic ? "yes" : "no", judgement.budget_exhausted ? "yes" : "no");
        }

        // T3.9.1: both judgements must be exact in both directions (§28.4)
        {
            std::size_t cyclic_rejected = 0;
            std::size_t cyclic_total = 0;
            std::size_t acyclic_accepted = 0;
            std::size_t acyclic_total = 0;

            for (const Judgement& judgement : judgements) {
                if (judgement.cyclic) {
                    cyclic_total++;
                    cyclic_rejected += judgement.detected_cycle && !judgement.order_proved_acyclic ? 1u : 0u;
                }
                else {
                    acyclic_total++;
                    acyclic_accepted += !judgement.detected_cycle && judgement.order_proved_acyclic ? 1u : 0u;
                }
            }

            check(verdict, "T3.9.1", std::string("0 false negatives and 0 false positives"), cyclic_rejected == cyclic_total && acyclic_accepted == acyclic_total, std::to_string(cyclic_rejected) + "/" + std::to_string(cyclic_total) + " cyclic rejected, " + std::to_string(acyclic_accepted) + "/" + std::to_string(acyclic_total) + " acyclic accepted");
        }

        // the conditional cycle: one runtime edge closes a ring the declaration did not have (§28.4)
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, false);
            const SchemaId schema = declare(scene);
            const ConditionalCycle conditional = build_conditional(scene, schema);

            // the order the seal built still claims acyclicity, which is the stale claim T3.9.4 is about
            const bool acyclic_before = albor::prototype_d2::seal_proved_acyclic(scene.graph());

            // add the conditional edge: the first link's binding now sums the second link, which already reads the
            // first, so the runtime graph holds a ring the declaration never declared and the value cannot settle
            // because both bindings read what the other writes (§18.1)
            static_cast<void>(scene.graph().add_dependency(scene.graph().writer_of(conditional.first), conditional.second));

            // Kahn after the mutation, which is the runtime guard a sound design needs (§7)
            const albor::prototype_d2::CycleReport after = albor::prototype_d2::detect_cycles(scene.graph());

            // drive the source, which is what puts the cycle in motion (§28.2)
            scene.graph().assign(conditional.source, Value(static_cast<std::int64_t>(5)));
            const albor::prototype_d2::SettleStats stats = scene.graph().settle();

            check(verdict, "T3.9.2", std::string("conditional cycle trips the 64-pass guard"), stats.cycle_budget_exhausted && stats.passes == albor::prototype_d2::MAX_SETTLE_PASSES, std::to_string(stats.passes) + " passes, guard exhausted=" + (stats.cycle_budget_exhausted ? "yes" : "no") + ", value left at " + format(static_cast<double>(scene.store().read(conditional.second).as_int()), 0));

            check(verdict, "T3.9.4", std::string("0 false acyclic claims after the mutation"), acyclic_before && after.has_cycle, std::string("order claimed acyclic before=") + (acyclic_before ? "yes" : "no") + ", Kahn finds a cycle after=" + (after.has_cycle ? "yes" : "no") + ", so a stale order is exactly the unsound case");

            record("-", std::string("conditional cycle: bindings"), std::to_string(conditional.bindings_before) + " before the conditional edge, cycle members " + std::to_string(after.members.size()));
        }

        // T3.9.3: what a runtime guard costs after one mutation, at the committed scale (§28.4)
        {
            Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, true);
            const SchemaId schema = declare(scene);

            // build the committed binding count as a chain, which is the shape detection walks (§7)
            std::vector<PropertyId> chain = albor::prototype_d2::build_chain(scene, schema, DETECTION_BINDINGS - 1);

            scene.graph().seal();
            static_cast<void>(scene.graph().settle());

            // warm the walk once, so the measurement is not the first touch of the tables (§28.4)
            static_cast<void>(albor::prototype_d2::detect_cycles(scene.graph()));

            std::vector<double> samples;
            samples.reserve(200);

            for (std::size_t iteration = 0; iteration < 200; iteration++) {
                // add one conditional edge, then re-run detection: the guard a sound design needs per batch (§18.1)
                const PropertyId tip = chain.at(chain.size() - 1);
                const Clock::time_point start = Clock::now();
                static_cast<void>(scene.graph().add_dependency(static_cast<BindingId>(0), tip));
                static_cast<void>(albor::prototype_d2::detect_cycles(scene.graph()));
                samples.push_back(to_ms(Clock::now() - start));
            }

            std::ranges::sort(samples);
            const double p99 = samples.at(std::min(samples.size() - 1, static_cast<std::size_t>(0.99 * static_cast<double>(samples.size()))));
            check(verdict, "T3.9.3", std::string("detection after one conditional edge p99 <= 2 ms"), p99 <= T3_9_3_DETECTION_MS, format(p99, 3) + " ms at " + std::to_string(DETECTION_BINDINGS) + " bindings, budget " + format(T3_9_3_DETECTION_MS, 1) + " ms");
        }

        // T3.9.5: the price of declaring the union, which is what keeps the static check sound (§28.4)
        {
            struct Churn
            {
                std::size_t invalidations = 0;
                std::size_t evaluations = 0;
                std::size_t index_bytes = 0;
            };

            const auto measure = [](const bool over_declared) {
                Scene<TypedStore> scene(albor::prototype_d2::EvalModel::EagerPush, true);
                const SchemaId schema = declare(scene);

                // a chain of links, each reading its predecessor and one source nobody writes (§13.3)
                const InstanceId source_instance = scene.store().create(schema);
                const PropertyId source = scene.at(source_instance, 0);
                static_cast<void>(scene.store().write(source, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));

                std::vector<PropertyId> chain;

                for (std::size_t index = 0; index < 500; index++) {
                    const InstanceId instance = scene.store().create(schema);
                    const PropertyId property = scene.at(instance, 0);
                    static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));

                    // the exact declaration reads the predecessor only; the union also reads the source it never
                    // reads while the condition is false, which is what over-declaration costs (§28.4)
                    if (over_declared) {
                        static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{index == 0 ? source : chain.back(), source}, &albor::prototype_d2::sum_all<TypedStore>));
                    }
                    else {
                        static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{index == 0 ? source : chain.back()}, &albor::prototype_d2::sum_all<TypedStore>));
                    }

                    chain.push_back(property);
                }

                scene.graph().seal();
                static_cast<void>(scene.graph().settle());
                scene.graph().reset_invalidated_total();

                // write the source 100 times, which is what a poll of an observable does while the condition is false
                for (std::size_t write = 0; write < 100; write++) {
                    scene.graph().assign(source, Value(static_cast<std::int64_t>(write + 2)));
                    static_cast<void>(scene.graph().settle());
                }

                Churn result;
                result.invalidations = scene.graph().invalidated_total();
                result.index_bytes = scene.graph().index_bytes();
                return result;
            };

            const Churn exact = measure(false);
            const Churn over = measure(true);
            const double ratio = static_cast<double>(over.invalidations) / static_cast<double>(std::max<std::size_t>(1, exact.invalidations));
            const double growth = static_cast<double>(over.index_bytes) / static_cast<double>(std::max<std::size_t>(1, exact.index_bytes)) - 1.0;

            check(verdict, "T3.9.5", std::string("union declaration: <= 2x invalidations, <= 25% index bytes"), ratio <= T3_9_5_INVALIDATION_RATIO && growth <= T3_9_5_INDEX_GROWTH, format(ratio, 2) + "x invalidations (" + std::to_string(exact.invalidations) + " exact vs " + std::to_string(over.invalidations) + " union), " + format(growth * 100.0, 1) + "% index bytes");

            record("-", std::string("union declaration cost"), std::to_string(exact.index_bytes) + " index bytes exact against " + std::to_string(over.index_bytes) + " union, over " + std::to_string(500) + " bindings");
        }

        // T3.9.6: no acyclic topology may trip the pass guard (§28.4)
        {
            std::size_t exhausted = 0;
            std::size_t acyclic = 0;

            for (const Judgement& judgement : judgements) {
                if (judgement.cyclic) {
                    continue;
                }

                acyclic++;

                if (judgement.budget_exhausted) {
                    exhausted++;
                }
            }

            check(verdict, "T3.9.6", std::string("no acyclic topology exhausts the 64-pass guard"), exhausted == 0, std::to_string(exhausted) + "/" + std::to_string(acyclic) + " acyclic topologies exhausted it");
        }
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R9 — static cycle rejection with conditional dependencies (AD-04c, §7)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R9, committed before this program existed\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_cycles(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
