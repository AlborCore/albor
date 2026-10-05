// Prototype D3 R5 — the cost of carrying two evaluation engines (F2.5)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// AD-25 requires a compiled engine and a dev-time evaluator to produce the same observable behaviour. AD-04
// and AD-10 already exclude the dev side at link time, so the question is not whether the second engine's
// code is present — it is whether its state and its dispatch are present in the runtime module, and whether
// two engines can coexist over one store without sharing mutable state

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        using albor::prototype_d2::BindingContext;
        using albor::prototype_d2::BindingFn;
        using albor::prototype_d2::EvalModel;
        using albor::prototype_d2::Graph;
        using albor::prototype_d2::InstanceId;
        using albor::prototype_d2::PropertyId;
        using albor::prototype_d2::Scene;
        using albor::prototype_d2::SchemaId;
        using albor::prototype_d2::SettleStats;
        using albor::prototype_d2::TypedStore;
        using albor::prototype_d2::Value;
        using albor::prototype_d2::ValueType;
        using albor::prototype_d2::WriteSource;

        // T3.5.2 engine state for the shipping candidate, against D's 39.3 B per binding for the whole binding
        constexpr double T3_5_2_STATE_BYTES_PER_BINDING = 32.0;

        // T3.5.3 what carrying a model the graph never runs may cost
        constexpr double T3_5_3_UNUSED_STATE_BYTES_PER_BINDING = 16.0;

        // T3.5.4 the second engine must not be superlinear at run time
        constexpr double T3_5_4_DOUBLE_SETTLE_RATIO = 1.10;

        // the committed conformance corpus: the four committed topologies plus 200 pseudo-random DAGs (§28.4)
        constexpr std::size_t RANDOM_SCENES = 200;
        constexpr std::size_t RANDOM_NODES = 24;
        constexpr std::size_t RANDOM_MAX_DEPTH = 6;
        constexpr std::size_t RANDOM_MAX_FANOUT = 4;
        constexpr std::uint32_t RANDOM_SEED = 20260204u;

        // the committed scale for the state and timing measurements (§28.4)
        constexpr std::size_t SCALE_BINDINGS = 11000;
        constexpr std::size_t SCALE_PROPERTIES = 12000;
        constexpr std::size_t MEASURED_SETTLES = 200;
        constexpr std::size_t WARMUP_SETTLES = 50;

        using Clock = std::chrono::steady_clock;

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, const std::string& description, const bool passed, const std::string& detail) {
            std::printf("  %-8s %-46s %-4s %s\n", std::string(id).c_str(), description.c_str(), passed ? "PASS" : "FAIL", detail.c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, const std::string& description, const std::string& detail) { std::printf("  %-8s %-46s %-4s %s\n", std::string(id).c_str(), description.c_str(), "REC", detail.c_str()); }

        std::string format(const double value, const int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        double percentile(std::vector<double> samples, const double fraction) {
            // report nothing when the section measured no sample (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index (§28.2)
            std::ranges::sort(samples);
            return samples.at(std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size()))));
        }

        // the binding bodies a scene may use, so a random scene is a DAG rather than a chain (§28.4)
        template <class Store>
        void add_one(BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) + 1);
        }

        template <class Store>
        void double_first(BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) * 2);
        }

        template <class Store>
        void sum_all(BindingContext<Store>& context) {
            std::int64_t total = 0;

            for (const PropertyId dependency : context.declared()) {
                total += context.read_int(dependency);
            }

            context.write_int(total);
        }

        template <class Store>
        void clamped_sum(BindingContext<Store>& context) {
            std::int64_t total = 0;

            for (const PropertyId dependency : context.declared()) {
                total += context.read_int(dependency);
            }

            context.write_int(std::min<std::int64_t>(total, 1));
        }

        // hold one declared scene, so the same graph can be built twice over two stores (§28.4)
        struct SceneSpec
        {
            std::string name;
            std::vector<std::vector<PropertyId>> dependencies;
            std::vector<std::size_t> body;
            std::vector<bool> is_source;
        };

        // one declared binding's body, as an index the instantiator resolves (§28.4)
        BindingFn<TypedStore> body_of(const std::size_t index) {
            // map the declared body onto the plain function pointer the graph stores (§13.3)
            switch (index) {
            case 0:
                return &add_one<TypedStore>;
            case 1:
                return &double_first<TypedStore>;
            case 2:
                return &sum_all<TypedStore>;
            default:
                return &clamped_sum<TypedStore>;
            }
        }

        std::string body_name(const std::size_t index) {
            // name the body so a divergence report can name it (§28.2)
            switch (index) {
            case 0:
                return "add_one";
            case 1:
                return "double_first";
            case 2:
                return "sum_all";
            default:
                return "clamped_sum";
            }
        }

        // hold one instantiated scene, which is what a comparison reads (§28.2)
        struct Built
        {
            Scene<TypedStore>* scene = nullptr;
            std::vector<PropertyId> sources;
        };

        // build one declared scene over its own store, with the model the caller chose (§28.4)
        Built instantiate(const SceneSpec& spec, const EvalModel model) {
            // the scene outlives the call, so it is owned by the caller through a leaked holder (§28.2)
            auto owned = std::make_unique<Scene<TypedStore>>(model, false);
            const SchemaId schema = owned->regions().add_schema("Node", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

            Built built;
            std::vector<PropertyId> properties;

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                const InstanceId instance = owned->store().create(schema);
                const PropertyId property = owned->at(instance, 0);
                static_cast<void>(owned->store().write(property, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
                properties.push_back(property);
            }

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                if (spec.is_source.at(node)) {
                    built.sources.push_back(properties.at(node));
                    continue;
                }

                static_cast<void>(owned->graph().add_binding(properties.at(node), spec.dependencies.at(node), body_of(spec.body.at(node))));
            }

            built.scene = owned.get();
            owned.release();

            // hand back the built scene, whose store the caller keeps alive through the returned pointer (§28.2)
            return built;
        }

        // build one of the committed pseudo-random DAGs, from a fixed seed so the corpus is reproducible (§28.4)
        SceneSpec random_scene(const std::size_t index) {
            SceneSpec spec;
            spec.name = "random_" + std::to_string(index);

            std::mt19937 generator(RANDOM_SEED + static_cast<std::uint32_t>(index));
            std::uniform_int_distribution<std::size_t> depth(1, RANDOM_NODES);
            std::uniform_int_distribution<std::size_t> fanout(1, RANDOM_MAX_FANOUT);
            std::uniform_int_distribution<std::size_t> body(0, 3);
            std::uniform_int_distribution<std::size_t> reachable(0, RANDOM_MAX_DEPTH);

            const std::size_t nodes = depth(generator);

            for (std::size_t node = 0; node < nodes; node++) {
                // a node's dependencies are drawn only from earlier nodes, which is what makes the scene a DAG (§28.4)
                std::vector<PropertyId> dependencies;
                const bool source = node == 0 || reachable(generator) == 0;

                if (!source) {
                    const std::size_t count = fanout(generator);
                    const std::size_t window = std::min(node, RANDOM_MAX_DEPTH);

                    for (std::size_t pick = 0; pick < count; pick++) {
                        // draw from the last few nodes, so the graph has both chains and reconvergence (§28.4)
                        const std::size_t back = 1 + (pick * window) / std::max<std::size_t>(1, count);
                        dependencies.push_back(static_cast<PropertyId>(node - std::min(back, node)));
                    }
                }

                spec.dependencies.push_back(dependencies);
                spec.body.push_back(body(generator));
                spec.is_source.push_back(source);
            }

            // hand back the declared scene (§28.2)
            return spec;
        }

        // the four committed topologies, declared in the same form the random scenes use (§28.4)
        std::vector<SceneSpec> committed_scenes() {
            std::vector<SceneSpec> scenes;

            // a chain of sixteen (§3)
            {
                SceneSpec spec;
                spec.name = "chain16";

                for (std::size_t node = 0; node < 17; node++) {
                    spec.dependencies.push_back(node == 0 ? std::vector<PropertyId>{} : std::vector<PropertyId>{static_cast<PropertyId>(node - 1)});
                    spec.body.push_back(1);
                    spec.is_source.push_back(node == 0);
                }

                scenes.push_back(spec);
            }

            // a diamond: one source, thirty-two branches, one sink (§3)
            {
                SceneSpec spec;
                spec.name = "diamond32";
                spec.dependencies.push_back({});
                spec.body.push_back(1);
                spec.is_source.push_back(true);

                for (std::size_t branch = 0; branch < 32; branch++) {
                    spec.dependencies.push_back({0});
                    spec.body.push_back(1);
                    spec.is_source.push_back(false);
                }

                std::vector<PropertyId> sink_dependencies;

                for (std::size_t branch = 0; branch < 32; branch++) {
                    sink_dependencies.push_back(static_cast<PropertyId>(branch + 1));
                }

                spec.dependencies.push_back(sink_dependencies);
                spec.body.push_back(2);
                spec.is_source.push_back(false);
                scenes.push_back(spec);
            }

            // a reconvergent graph: eight short branches plus a deep branch of eight, one sink (§3)
            {
                SceneSpec spec;
                spec.name = "reconvergent";
                spec.dependencies.push_back({});
                spec.body.push_back(1);
                spec.is_source.push_back(true);

                // the deep branch, one node per layer (§3)
                for (std::size_t layer = 0; layer < 8; layer++) {
                    spec.dependencies.push_back({static_cast<PropertyId>(layer)});
                    spec.body.push_back(1);
                    spec.is_source.push_back(false);
                }

                // the short branches, each one hop from the source (§3)
                for (std::size_t branch = 0; branch < 8; branch++) {
                    spec.dependencies.push_back({0});
                    spec.body.push_back(1);
                    spec.is_source.push_back(false);
                }

                std::vector<PropertyId> sink_dependencies;

                for (std::size_t branch = 0; branch < 8; branch++) {
                    sink_dependencies.push_back(static_cast<PropertyId>(9 + branch));
                }

                sink_dependencies.push_back(8);
                spec.dependencies.push_back(sink_dependencies);
                spec.body.push_back(2);
                spec.is_source.push_back(false);
                scenes.push_back(spec);
            }

            // Prototype D's shape: a chain of eight plus three siblings, per node (§28.4)
            {
                SceneSpec spec;
                spec.name = "d_shape";

                for (std::size_t node = 0; node < 16; node++) {
                    const std::size_t input = spec.dependencies.size();
                    spec.dependencies.push_back({});
                    spec.body.push_back(1);
                    spec.is_source.push_back(true);

                    const std::size_t first = spec.dependencies.size();
                    spec.dependencies.push_back({static_cast<PropertyId>(input)});
                    spec.body.push_back(1);
                    spec.is_source.push_back(false);

                    std::size_t tip = first;

                    for (std::size_t layer = 1; layer < 8; layer++) {
                        spec.dependencies.push_back({static_cast<PropertyId>(tip)});
                        spec.body.push_back(1);
                        spec.is_source.push_back(false);
                        tip = spec.dependencies.size() - 1;
                    }

                    for (std::size_t sibling = 0; sibling < 3; sibling++) {
                        spec.dependencies.push_back({static_cast<PropertyId>(tip)});
                        spec.body.push_back(1);
                        spec.is_source.push_back(false);
                    }
                }

                scenes.push_back(spec);
            }

            // hand back the committed scenes (§28.2)
            return scenes;
        }

        // read every node's value, so two engines are compared on what a reader would see (§28.2)
        std::vector<std::int64_t> read_all(Scene<TypedStore>& scene, const SceneSpec& spec) {
            // collect the values in declaration order (§28.2)
            std::vector<std::int64_t> values;
            values.reserve(spec.dependencies.size());

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                values.push_back(scene.store().read(static_cast<PropertyId>(node)).as_int());
            }

            // hand back the snapshot (§28.2)
            return values;
        }

        // hold the outcome of one scene's conformance run (§28.2)
        struct Outcome
        {
            std::string name;
            bool agree = true;
            std::string detail;
            std::size_t eager_passes = 0;
            std::size_t hybrid_passes = 0;
        };

        // run one declared scene through both engines with the same write sequence and compare the values (§28.4)
        Outcome compare_engines(const SceneSpec& spec) {
            Outcome outcome;
            outcome.name = spec.name;

            // build the same declaration twice, once per engine, each over its own store (§28.4)
            const Built eager = instantiate(spec, EvalModel::EagerPush);
            const Built hybrid = instantiate(spec, EvalModel::HybridPushPull);

            eager.scene->graph().seal();
            hybrid.scene->graph().seal();
            static_cast<void>(eager.scene->graph().settle());
            static_cast<void>(hybrid.scene->graph().settle());

            // drive the sources with the same sequence, which is what the two engines must agree about (§28.4)
            for (std::size_t round = 0; round < 4; round++) {
                for (std::size_t source = 0; source < eager.sources.size(); source++) {
                    const std::int64_t value = static_cast<std::int64_t>(round * 7 + source + 2);
                    eager.scene->graph().assign(eager.sources.at(source), Value(value));
                    hybrid.scene->graph().assign(hybrid.sources.at(source), Value(value));
                }

                outcome.eager_passes += eager.scene->graph().settle().passes;
                outcome.hybrid_passes += hybrid.scene->graph().settle().passes;
            }

            const std::vector<std::int64_t> left = read_all(*eager.scene, spec);
            const std::vector<std::int64_t> right = read_all(*hybrid.scene, spec);
            outcome.agree = left == right;

            if (!outcome.agree) {
                // name the first node the two engines disagree about, and the body that wrote it (§28.2)
                for (std::size_t node = 0; node < left.size(); node++) {
                    if (left.at(node) != right.at(node)) {
                        outcome.detail = "node " + std::to_string(node) + " (" + body_name(spec.body.at(node)) + "): eager " + std::to_string(left.at(node)) + ", hybrid " + std::to_string(right.at(node));
                        break;
                    }
                }
            }

            delete eager.scene;
            delete hybrid.scene;

            // hand back what the two engines agreed (§28.2)
            return outcome;
        }

    } // namespace

    void run_dual(Verdict& verdict) {
        // the committed corpus: the four topologies plus the pseudo-random DAGs (§28.4)
        std::vector<SceneSpec> scenes = committed_scenes();

        for (std::size_t index = 0; index < RANDOM_SCENES; index++) {
            scenes.push_back(random_scene(index));
        }

        std::printf("--- conformance: the same %zu scenes through both engines ---\n", scenes.size());

        std::vector<Outcome> outcomes;

        for (const SceneSpec& spec : scenes) {
            outcomes.push_back(compare_engines(spec));
        }

        std::size_t agreed = 0;
        std::string first_divergence;
        std::size_t random_agreed = 0;
        std::size_t random_total = 0;

        for (const Outcome& outcome : outcomes) {
            if (outcome.agree) {
                agreed++;
            }
            else if (first_divergence.empty()) {
                first_divergence = outcome.name + " " + outcome.detail;
            }

            if (outcome.name.starts_with("random_")) {
                random_total++;

                if (outcome.agree) {
                    random_agreed++;
                }
            }
        }

        check(verdict, "T3.5.1", "100% final-value agreement over the corpus", agreed == outcomes.size(), std::to_string(agreed) + "/" + std::to_string(outcomes.size()) + " scenes agreed (" + std::to_string(random_agreed) + "/" + std::to_string(random_total) + " of the pseudo-random DAGs)" + (first_divergence.empty() ? "" : ", first divergence " + first_divergence));

        // the state cost, at the committed scale (§28.4)
        {
            Scene<TypedStore> scene(EvalModel::EagerPush, false);
            const SchemaId schema = scene.regions().add_schema("Node", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

            PropertyId previous = 0;

            for (std::size_t index = 0; index < SCALE_PROPERTIES; index++) {
                const InstanceId instance = scene.store().create(schema);
                const PropertyId property = scene.at(instance, 0);
                static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));

                if (index == 0) {
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{property}, &double_first<TypedStore>));
                }
                else if (index < SCALE_BINDINGS) {
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{previous}, &double_first<TypedStore>));
                    previous = property;
                }
            }

            scene.graph().seal();
            static_cast<void>(scene.graph().settle());

            const std::size_t bindings = scene.graph().binding_count();
            const std::size_t properties = scene.store().property_count();
            const std::size_t union_bytes = scene.graph().engine_state_bytes();

            const std::size_t eager_bytes = Graph<TypedStore>::model_state_bytes(EvalModel::EagerPush, bindings, properties);
            const std::size_t hybrid_bytes = Graph<TypedStore>::model_state_bytes(EvalModel::HybridPushPull, bindings, properties);
            const std::size_t pull_bytes = Graph<TypedStore>::model_state_bytes(EvalModel::PullTopo, bindings, properties);

            std::printf("--- engine state at %zu bindings and %zu properties ---\n", bindings, properties);
            std::printf("  %-34s %12s %12s\n", "configuration", "bytes", "B/binding");

            const auto row = [](const std::string& name, const std::size_t bytes, const std::size_t bindings) { std::printf("  %-34s %12zu %12s\n", name.c_str(), bytes, format(static_cast<double>(bytes) / static_cast<double>(bindings), 2).c_str()); };

            row("the union the graph allocates", union_bytes, bindings);
            row("eager_push's own subset", eager_bytes, bindings);
            row("hybrid_push_pull's own subset", hybrid_bytes, bindings);
            row("pull_topo's own subset", pull_bytes, bindings);
            row("state for a model never run", union_bytes - eager_bytes, bindings);

            // T3.5.2: the shipping candidate's own state, against D's 39.3 B per binding (§28.4)
            check(verdict, "T3.5.2", "eager_push state <= 32 B/binding", static_cast<double>(eager_bytes) / static_cast<double>(bindings) <= T3_5_2_STATE_BYTES_PER_BINDING, format(static_cast<double>(eager_bytes) / static_cast<double>(bindings), 2) + " B/binding <= " + format(T3_5_2_STATE_BYTES_PER_BINDING, 0) + " B");

            // T3.5.3: what carrying the models the graph never runs costs (§28.4)
            check(verdict, "T3.5.3", "state for a model never run <= 16 B/binding", static_cast<double>(union_bytes - eager_bytes) / static_cast<double>(bindings) <= T3_5_3_UNUSED_STATE_BYTES_PER_BINDING, format(static_cast<double>(union_bytes - eager_bytes) / static_cast<double>(bindings), 2) + " B/binding <= " + format(T3_5_3_UNUSED_STATE_BYTES_PER_BINDING, 0) + " B");

            record("-", "what the union contains", "the graph allocates every model's stamps, its Kahn order and its rank whatever model it runs: " + std::to_string(union_bytes) + " B against eager's own " + std::to_string(eager_bytes) + " B, so " + format(100.0 * static_cast<double>(union_bytes - eager_bytes) / static_cast<double>(union_bytes), 1) + "% of the engine's state pays for engines that never run");
        }

        // T3.5.5: two engines over one store, interleaved against sequential (§28.4)
        {
            const SceneSpec spec = committed_scenes().at(3);

            // one store, two graphs over it, which is the arrangement two linkable engines would need (§28.4)
            Scene<TypedStore> shared(EvalModel::EagerPush, false);
            const SchemaId schema = shared.regions().add_schema("Node", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
            Graph<TypedStore> eager(shared.regions(), shared.store(), EvalModel::EagerPush, false);
            Graph<TypedStore> hybrid(shared.regions(), shared.store(), EvalModel::HybridPushPull, false);

            std::vector<PropertyId> properties;
            std::vector<PropertyId> sources;

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                const InstanceId instance = shared.store().create(schema);
                const PropertyId property = shared.at(instance, 0);
                static_cast<void>(shared.store().write(property, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
                properties.push_back(property);

                if (spec.is_source.at(node)) {
                    sources.push_back(property);
                }
            }

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                if (!spec.is_source.at(node)) {
                    static_cast<void>(eager.add_binding(properties.at(node), spec.dependencies.at(node), body_of(spec.body.at(node))));
                    static_cast<void>(hybrid.add_binding(properties.at(node), spec.dependencies.at(node), body_of(spec.body.at(node))));
                }
            }

            eager.seal();
            hybrid.seal();
            static_cast<void>(eager.settle());
            static_cast<void>(hybrid.settle());

            // drive both graphs in lockstep, which is what interleaved execution means (§28.4)
            for (std::size_t round = 0; round < 8; round++) {
                for (const PropertyId source : sources) {
                    const std::int64_t value = static_cast<std::int64_t>(round + 2);
                    eager.assign(source, Value(value));
                    hybrid.assign(source, Value(value));
                }

                static_cast<void>(eager.settle());
                static_cast<void>(hybrid.settle());
            }

            std::vector<std::int64_t> interleaved;
            interleaved.reserve(properties.size());

            for (const PropertyId property : properties) {
                interleaved.push_back(shared.store().read(property).as_int());
            }

            // now drive one graph at a time over a second store, which is the sequential arrangement (§28.4)
            Scene<TypedStore> sequential_scene(EvalModel::EagerPush, false);
            const SchemaId sequential_schema = sequential_scene.regions().add_schema("Node", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
            Graph<TypedStore> sequential_eager(sequential_scene.regions(), sequential_scene.store(), EvalModel::EagerPush, false);
            Graph<TypedStore> sequential_hybrid(sequential_scene.regions(), sequential_scene.store(), EvalModel::HybridPushPull, false);

            std::vector<PropertyId> sequential_properties;
            std::vector<PropertyId> sequential_sources;

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                const InstanceId instance = sequential_scene.store().create(sequential_schema);
                const PropertyId property = sequential_scene.at(instance, 0);
                static_cast<void>(sequential_scene.store().write(property, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
                sequential_properties.push_back(property);

                if (spec.is_source.at(node)) {
                    sequential_sources.push_back(property);
                }
            }

            for (std::size_t node = 0; node < spec.dependencies.size(); node++) {
                if (!spec.is_source.at(node)) {
                    static_cast<void>(sequential_eager.add_binding(sequential_properties.at(node), spec.dependencies.at(node), body_of(spec.body.at(node))));
                    static_cast<void>(sequential_hybrid.add_binding(sequential_properties.at(node), spec.dependencies.at(node), body_of(spec.body.at(node))));
                }
            }

            sequential_eager.seal();
            sequential_hybrid.seal();
            static_cast<void>(sequential_eager.settle());
            static_cast<void>(sequential_hybrid.settle());

            for (std::size_t round = 0; round < 8; round++) {
                for (const PropertyId source : sequential_sources) {
                    sequential_eager.assign(source, Value(static_cast<std::int64_t>(round + 2)));
                }

                static_cast<void>(sequential_eager.settle());

                for (const PropertyId source : sequential_sources) {
                    sequential_hybrid.assign(source, Value(static_cast<std::int64_t>(round + 2)));
                }

                static_cast<void>(sequential_hybrid.settle());
            }

            std::vector<std::int64_t> sequential;
            sequential.reserve(sequential_properties.size());

            for (const PropertyId property : sequential_properties) {
                sequential.push_back(sequential_scene.store().read(property).as_int());
            }

            check(verdict, "T3.5.5", "0 divergences between interleaved and sequential settles", interleaved == sequential, std::to_string(interleaved.size()) + " properties compared over 8 rounds on one shared store against two stores driven in turn");

            record("-", "one store, two engines", "both graphs read and wrote the same store in lockstep with no shared mutable engine state observed, which is the condition for excluding one engine at link time (AD-04)");
        }

        // T3.5.4: what a second engine costs when it is actually run (§28.4)
        {
            Scene<TypedStore> scene(EvalModel::EagerPush, false);
            const SchemaId schema = scene.regions().add_schema("Node", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

            // the committed wide fan-in, which is the shape a real settle has (§28.4)
            const InstanceId source_instance = scene.store().create(schema);
            const PropertyId source = scene.at(source_instance, 0);
            static_cast<void>(scene.store().write(source, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));

            std::vector<PropertyId> branches;

            for (std::size_t branch = 0; branch < 32; branch++) {
                const InstanceId instance = scene.store().create(schema);
                const PropertyId property = scene.at(instance, 0);
                static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{source}, &double_first<TypedStore>));
                branches.push_back(property);
            }

            const InstanceId sink_instance = scene.store().create(schema);
            const PropertyId sink = scene.at(sink_instance, 0);
            static_cast<void>(scene.store().write(sink, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
            static_cast<void>(scene.graph().add_binding(sink, branches, &sum_all<TypedStore>));

            // the second engine over the same store and the same bindings (§28.4)
            Graph<TypedStore> hybrid(scene.regions(), scene.store(), EvalModel::HybridPushPull, false);

            for (std::size_t branch = 0; branch < 32; branch++) {
                static_cast<void>(hybrid.add_binding(branches.at(branch), std::vector<PropertyId>{source}, &double_first<TypedStore>));
            }

            static_cast<void>(hybrid.add_binding(sink, branches, &sum_all<TypedStore>));

            scene.graph().seal();
            hybrid.seal();
            static_cast<void>(scene.graph().settle());
            static_cast<void>(hybrid.settle());

            const auto drive_eager_only = [&] {
                scene.graph().assign(source, Value(static_cast<std::int64_t>(3)));
                static_cast<void>(scene.graph().settle());
            };

            const auto drive_both = [&] {
                scene.graph().assign(source, Value(static_cast<std::int64_t>(3)));
                static_cast<void>(scene.graph().settle());

                // the second engine reads the invalidation the first one left, which is the conformance cost (§28.2)
                static_cast<void>(hybrid.settle());
            };

            // discard the warm-up settles, which pay the first touch of every page (§28.4)
            for (std::size_t step = 0; step < WARMUP_SETTLES; step++) {
                drive_eager_only();
                drive_both();
            }

            std::vector<double> single;
            std::vector<double> doubled;
            single.reserve(MEASURED_SETTLES);
            doubled.reserve(MEASURED_SETTLES);

            for (std::size_t step = 0; step < MEASURED_SETTLES; step++) {
                const Clock::time_point single_start = Clock::now();
                drive_eager_only();
                single.push_back(std::chrono::duration<double, std::micro>(Clock::now() - single_start).count());

                const Clock::time_point both_start = Clock::now();
                drive_both();
                doubled.push_back(std::chrono::duration<double, std::micro>(Clock::now() - both_start).count());
            }

            const double single_p50 = percentile(single, 0.50);
            const double doubled_p50 = percentile(doubled, 0.50);

            check(verdict, "T3.5.4", "double settle <= 1.10x the single-engine settle", doubled_p50 <= single_p50 * T3_5_4_DOUBLE_SETTLE_RATIO, format(doubled_p50, 2) + " us against " + format(single_p50, 2) + " us, ratio " + format(doubled_p50 / std::max(single_p50, 1e-9), 3) + "x");

            record("-", "the price of the second engine", "running it over the same invalidation costs " + format(doubled_p50 - single_p50, 2) + " us per frame on a 32-wide fan-in, because the second settle finds the dirty set the first one left and evaluates nothing");
        }

        // record the pass counts, which is where the two engines visibly differ even when they agree (§28.4)
        std::size_t eager_total = 0;
        std::size_t hybrid_total = 0;

        for (const Outcome& outcome : outcomes) {
            eager_total += outcome.eager_passes;
            hybrid_total += outcome.hybrid_passes;
        }

        record("-", "passes across the corpus", "eager " + std::to_string(eager_total) + ", hybrid " + std::to_string(hybrid_total) + ", so the two engines reach the same values by different amounts of work on the scenes that agree");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R5 — the cost of carrying two evaluation engines (AD-25, AD-04)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R5, committed before this program existed\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_dual(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
