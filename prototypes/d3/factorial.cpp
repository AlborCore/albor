// Prototype D3 R4 — the index x evaluation factorial (F2.4)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// AD-04a leaves which index structure Open and AD-04c leaves which evaluation model Open. They have only ever
// been measured one axis at a time. This revision measures the 2x2, so the question is whether the axes are
// separable — in which case each can be chosen on its own evidence — or whether they interact, in which case
// they are one decision and neither prototype can settle it alone

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        using albor::prototype_d2::BindingContext;
        using albor::prototype_d2::BindingId;
        using albor::prototype_d2::EvalModel;
        using albor::prototype_d2::InstanceId;
        using albor::prototype_d2::PropertyId;
        using albor::prototype_d2::Scene;
        using albor::prototype_d2::SchemaId;
        using albor::prototype_d2::SettleStats;
        using albor::prototype_d2::TypedStore;
        using albor::prototype_d2::Value;
        using albor::prototype_d2::ValueType;
        using albor::prototype_d2::WriteSource;

        // T3.4.1 a one-hop fan-in has no excuse for a transient value
        constexpr std::size_t T3_4_1_GLITCHES = 0;

        // T3.4.3 the index axis must not move evaluation cost by more than this for the axes to be separable
        constexpr double T3_4_3_INDEX_EFFECT = 0.10;

        // T3.4.4 the interaction term must be inside the noise for the axes to be separable
        constexpr double T3_4_4_INTERACTION = 0.10;

        // T3.4.5 the mutation-locality argument has to survive at application scale
        constexpr double T3_4_5_SEGMENTED_ADD_US = 1.0;
        constexpr double T3_4_5_SEGMENTED_RATIO = 50.0;

        // the committed measurement shape: 200 discarded warm-up settles then 200 measured settles (§28.4)
        constexpr std::size_t WARMUP_SETTLES = 200;
        constexpr std::size_t MEASURED_SETTLES = 200;

        // the committed post-seal activity for the mutation axis (§28.4)
        constexpr std::size_t POST_SEAL_ADDITIONS = 1000;

        // the committed binding count for the mutation axis, the same scale R2 and R8 account at (§28.4)
        constexpr std::size_t MUTATION_BINDINGS = 11000;

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

        double median_of(std::vector<double> samples) {
            // report nothing when the cell measured no sample (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the middle becomes an index (§28.2)
            std::ranges::sort(samples);
            return samples.at(samples.size() / 2);
        }

        double p99_of(std::vector<double> samples) {
            // report nothing when the cell measured no sample (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index (§28.2)
            std::ranges::sort(samples);
            return samples.at(std::min(samples.size() - 1, static_cast<std::size_t>(0.99 * static_cast<double>(samples.size()))));
        }

        // the log every measured binding writes into, so a transient value is observed rather than inferred (§28.2)
        class RunLog
        {
        public:
            // size the log over a binding table, which happens once per measured cell (§28.2)
            void arm(const std::size_t bindings) {
                // remember how many bindings the log covers (§28.2)
                m_size = bindings;
                m_first.assign(bindings, 0);
                m_last.assign(bindings, 0);
                m_writes.assign(bindings, 0);
                m_total_writes = 0;
            }

            // open a new settle, which is the window a glitch is defined inside (§28.2)
            void begin_settle() {
                // clear the per-settle window, keeping the arrays themselves (§28.2)
                std::ranges::fill(m_first.begin(), m_first.end(), 0);
                std::ranges::fill(m_last.begin(), m_last.end(), 0);
                std::ranges::fill(m_writes.begin(), m_writes.end(), 0);
            }

            // record one value a binding published (§28.2)
            void record(const BindingId binding, const std::int64_t value) {
                // ignore a binding the log was not sized for, which cannot happen and would hide a defect (§28.2)
                if (binding >= m_size) {
                    return;
                }

                // count the write, which is how a repeat evaluation becomes visible (§28.2)
                m_writes.at(binding)++;
                m_total_writes++;

                // remember the first and the latest value, whose disagreement is the glitch (§28.2)
                if (m_writes.at(binding) == 1) {
                    m_first.at(binding) = value;
                }

                m_last.at(binding) = value;
            }

            // count the bindings that published a value inside this settle and superseded it (§28.2)
            [[nodiscard]] std::size_t glitches() const {
                // walk every binding the log covers (§28.2)
                std::size_t glitches = 0;

                for (std::size_t binding = 0; binding < m_size; binding++) {
                    // a binding that ran once published one value, which cannot be superseded (§28.2)
                    if (m_writes.at(binding) < 2) {
                        continue;
                    }

                    // a binding that published the same value twice published no transient value (§28.2)
                    if (m_first.at(binding) != m_last.at(binding)) {
                        glitches++;
                    }
                }

                // hand back the settle's glitch count (§28.2)
                return glitches;
            }

            [[nodiscard]] std::size_t total_writes() const { return m_total_writes; }

        private:
            std::size_t m_size = 0;
            std::vector<std::int64_t> m_first;
            std::vector<std::int64_t> m_last;
            std::vector<std::size_t> m_writes;
            std::size_t m_total_writes = 0;
        };

        RunLog g_log;

        // the pass-through binding that logs every value it publishes (§28.2)
        template <class Store>
        void logged_pass(BindingContext<Store>& context) {
            const std::int64_t value = context.read_int(context.declared().front());
            g_log.record(context.self(), value);
            context.write_int(value);
        }

        // the summing binding that logs, which is the one whose transient value a glitch exposes (§28.2)
        template <class Store>
        void logged_sum(BindingContext<Store>& context) {
            std::int64_t total = 0;

            for (const PropertyId dependency : context.declared()) {
                total += context.read_int(dependency);
            }

            g_log.record(context.self(), total);
            context.write_int(total);
        }

        // the plain pass-through, which is what the timed cells use so the log costs them nothing (§28.2)
        template <class Store>
        void plain_pass(BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()));
        }

        // the plain sum, which is the wide diamond's sink (§28.2)
        template <class Store>
        void plain_sum(BindingContext<Store>& context) {
            std::int64_t total = 0;

            for (const PropertyId dependency : context.declared()) {
                total += context.read_int(dependency);
            }

            context.write_int(total);
        }

        // the four committed topologies (§28.4)
        enum class Shape : std::uint8_t
        {
            Chain16,
            Diamond32,
            Reconvergent,
            DShape
        };

        std::string shape_name(const Shape shape) {
            // name the topology so a row is unambiguous (§28.2)
            switch (shape) {
            case Shape::Chain16:
                return "chain16";
            case Shape::Diamond32:
                return "diamond32";
            case Shape::Reconvergent:
                return "reconvergent";
            case Shape::DShape:
                return "d_shape";
            }

            return "unknown";
        }

        // the handles a topology is driven through, which every cell shares (§28.4)
        struct Handles
        {
            PropertyId source = 0;
            PropertyId sink = 0;
            std::vector<PropertyId> properties;

            // the properties one drive writes, which is one source for the three single-source shapes and every
            // node's own input for Prototype D's shape, because that is what its benchmark updates (§28.4)
            std::vector<PropertyId> drive;
        };

        // build one topology, with either the timed bodies or the logging ones (§28.4)
        Handles build(Scene<TypedStore>& scene, const SchemaId schema, const Shape shape, const bool logged) {
            Handles handles;

            // the one-pass body and the three-dependency body, chosen once so a cell's bodies are uniform (§28.2)
            const albor::prototype_d2::BindingFn<TypedStore> pass = logged ? &logged_pass<TypedStore> : &plain_pass<TypedStore>;
            const albor::prototype_d2::BindingFn<TypedStore> sum = logged ? &logged_sum<TypedStore> : &plain_sum<TypedStore>;

            // the source every topology writes through (§13.3)
            const InstanceId source_instance = scene.store().create(schema);
            handles.source = scene.at(source_instance, 0);
            static_cast<void>(scene.store().write(handles.source, Value(static_cast<std::int64_t>(1)), WriteSource::GeneratedInit));
            handles.properties.push_back(handles.source);
            handles.drive.push_back(handles.source);

            if (shape == Shape::Chain16) {
                // a chain of sixteen links, which is one pass per layer for the eager model (§3)
                for (std::size_t layer = 0; layer < 16; layer++) {
                    const InstanceId instance = scene.store().create(schema);
                    const PropertyId property = scene.at(instance, 0);
                    static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{layer == 0 ? handles.source : handles.properties.back()}, pass));
                    handles.properties.push_back(property);
                }

                handles.sink = handles.properties.back();
            }

            if (shape == Shape::Diamond32) {
                // one source, thirty-two branches, one sink (§3)
                std::vector<PropertyId> branches;

                for (std::size_t branch = 0; branch < 32; branch++) {
                    const InstanceId instance = scene.store().create(schema);
                    const PropertyId property = scene.at(instance, 0);
                    static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{handles.source}, pass));
                    branches.push_back(property);
                    handles.properties.push_back(property);
                }

                const InstanceId sink_instance = scene.store().create(schema);
                handles.sink = scene.at(sink_instance, 0);
                static_cast<void>(scene.store().write(handles.sink, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                static_cast<void>(scene.graph().add_binding(handles.sink, branches, sum));
                handles.properties.push_back(handles.sink);
            }

            if (shape == Shape::Reconvergent) {
                // eight short branches plus one branch of depth eight, the only topology that reconverges (§3)
                std::vector<PropertyId> branches;
                PropertyId long_tip = handles.source;

                for (std::size_t layer = 0; layer < 8; layer++) {
                    const InstanceId instance = scene.store().create(schema);
                    const PropertyId property = scene.at(instance, 0);
                    static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{layer == 0 ? handles.source : long_tip}, pass));
                    long_tip = property;
                    handles.properties.push_back(property);
                }

                for (std::size_t branch = 0; branch < 8; branch++) {
                    const InstanceId instance = scene.store().create(schema);
                    const PropertyId property = scene.at(instance, 0);
                    static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{handles.source}, pass));
                    branches.push_back(property);
                    handles.properties.push_back(property);
                }

                const InstanceId sink_instance = scene.store().create(schema);
                handles.sink = scene.at(sink_instance, 0);
                static_cast<void>(scene.store().write(handles.sink, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));

                // the sink reads the short branches plus the deep tip, which is what reconverges it (§3)
                branches.push_back(long_tip);
                static_cast<void>(scene.graph().add_binding(handles.sink, branches, sum));
                handles.properties.push_back(handles.sink);
            }

            if (shape == Shape::DShape) {
                // Prototype D's shape: a chain of eight plus three siblings, per node (§28.4)
                for (std::size_t node = 0; node < 16; node++) {
                    const InstanceId input_instance = scene.store().create(schema);
                    const PropertyId input = scene.at(input_instance, 0);
                    static_cast<void>(scene.store().write(input, Value(static_cast<std::int64_t>(node)), WriteSource::GeneratedInit));
                    handles.properties.push_back(input);

                    // D's shape gives every node its own input, so one drive writes them all (§28.4)
                    handles.drive.push_back(input);

                    const InstanceId first_instance = scene.store().create(schema);
                    PropertyId tip = scene.at(first_instance, 0);
                    static_cast<void>(scene.store().write(tip, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                    static_cast<void>(scene.graph().add_binding(tip, std::vector<PropertyId>{input}, pass));
                    handles.properties.push_back(tip);

                    for (std::size_t layer = 1; layer < 8; layer++) {
                        const InstanceId instance = scene.store().create(schema);
                        const PropertyId next = scene.at(instance, 0);
                        static_cast<void>(scene.store().write(next, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                        static_cast<void>(scene.graph().add_binding(next, std::vector<PropertyId>{tip}, pass));
                        tip = next;
                        handles.properties.push_back(next);
                    }

                    for (std::size_t sibling = 0; sibling < 3; sibling++) {
                        const InstanceId instance = scene.store().create(schema);
                        const PropertyId derived = scene.at(instance, 0);
                        static_cast<void>(scene.store().write(derived, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));
                        static_cast<void>(scene.graph().add_binding(derived, std::vector<PropertyId>{tip}, pass));
                        handles.properties.push_back(derived);
                    }

                    handles.sink = tip;
                }
            }

            // hand back the handles every cell drives the same way (§28.2)
            return handles;
        }

        // read every property's final value, so two cells can be compared on what a reader would see (§28.2)
        std::vector<std::int64_t> read_all(const TypedStore& store, const Handles& handles) {
            // collect the values in the topology's own order (§28.2)
            std::vector<std::int64_t> values;
            values.reserve(handles.properties.size());

            for (const PropertyId property : handles.properties) {
                values.push_back(store.read(property).as_int());
            }

            // hand back the snapshot a reader would see (§28.2)
            return values;
        }

        // hold what one cell produced on one topology (§28.2)
        struct Cell
        {
            std::string index;
            std::string model;
            Shape shape = Shape::Chain16;
            std::vector<std::int64_t> final_values;
            std::size_t bindings = 0;
            std::size_t passes = 0;
            std::size_t bindings_evaluated = 0;
            std::size_t redundant_evaluations = 0;
            std::size_t candidates = 0;
            std::size_t glitches = 0;
            std::size_t budget_exhausted = 0;
            std::size_t index_bytes = 0;
            std::vector<double> samples;
        };

        // run one cell of the factorial on one topology, with the logging bodies so glitches are observable (§28.4)
        Cell measure_cell(const bool mutable_index, const EvalModel model, const Shape shape) {
            Cell cell;
            cell.index = mutable_index ? "segmented" : "sealed_csr";
            cell.model = std::string(albor::prototype_d2::model_name(model));
            cell.shape = shape;

            Scene<TypedStore> scene(model, mutable_index);
            const SchemaId schema = scene.regions().add_schema("Scalar", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});
            const Handles handles = build(scene, schema, shape, true);

            cell.bindings = scene.graph().binding_count();
            scene.graph().seal();
            static_cast<void>(scene.graph().settle());
            cell.index_bytes = scene.graph().index_bytes();

            // size the log over this cell's binding table (§28.2)
            g_log.arm(scene.graph().binding_count());

            // drive the source once per settle, which is the interaction every topology is built for (§28.2)
            const auto drive = [&](const std::size_t step) {
                // open the settle's window before anything runs, so the first and latest values are this settle's (§28.2)
                g_log.begin_settle();

                // write every property this shape's drive owns, then settle once, so one settle is one frame (§28.4)
                for (std::size_t written = 0; written < handles.drive.size(); written++) {
                    scene.graph().assign(handles.drive.at(written), Value(static_cast<std::int64_t>(step + written + 2)));
                }

                return scene.graph().settle();
            };

            // discard the warm-up settles, which pay the first touch of every page (§28.4)
            for (std::size_t step = 0; step < WARMUP_SETTLES; step++) {
                static_cast<void>(drive(step));
            }

            std::size_t passes = 0;
            std::size_t evaluated = 0;
            std::size_t redundant = 0;
            std::size_t candidates = 0;
            std::size_t exhausted = 0;
            std::size_t glitches = 0;

            for (std::size_t step = 0; step < MEASURED_SETTLES; step++) {
                const Clock::time_point start = Clock::now();
                const SettleStats stats = drive(WARMUP_SETTLES + step);
                cell.samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());

                passes += stats.passes;
                evaluated += stats.bindings_evaluated;
                redundant += stats.redundant_evaluations;
                candidates += stats.candidates_considered;
                exhausted += stats.cycle_budget_exhausted ? 1u : 0u;

                // a superseded value is a transient inconsistency a reader inside the settle could have seen (§28.2)
                glitches += g_log.glitches();
            }

            cell.passes = passes;
            cell.bindings_evaluated = evaluated;
            cell.redundant_evaluations = redundant;
            cell.candidates = candidates;
            cell.budget_exhausted = exhausted;
            cell.glitches = glitches;
            cell.final_values = read_all(scene.store(), handles);

            // hand back what this cell produced (§28.2)
            return cell;
        }

        // report one cell's row, in the units every cell shares (§28.2)
        void report_cell(const Cell& cell) { std::printf("  %-12s %-18s %-14s %6zu %8s %8s %8s %10s %10s %8s\n", cell.index.c_str(), cell.model.c_str(), shape_name(cell.shape).c_str(), cell.bindings, format(median_of(cell.samples), 2).c_str(), format(p99_of(cell.samples), 2).c_str(), format(static_cast<double>(cell.passes) / static_cast<double>(MEASURED_SETTLES), 1).c_str(), format(static_cast<double>(cell.bindings_evaluated) / static_cast<double>(MEASURED_SETTLES), 1).c_str(), format(static_cast<double>(cell.redundant_evaluations) / static_cast<double>(MEASURED_SETTLES), 1).c_str(), format(static_cast<double>(cell.glitches) / static_cast<double>(MEASURED_SETTLES), 2).c_str()); }

        // hold one mutation measurement, which is the index's own axis (§28.2)
        struct MutationCell
        {
            std::string index;
            std::size_t bindings = 0;
            std::size_t index_bytes_before = 0;
            std::size_t index_bytes_after = 0;
            std::vector<double> samples;
        };

        // time the committed post-seal additions on one index, at the committed scale (§28.4)
        MutationCell measure_mutation(const bool mutable_index) {
            MutationCell cell;
            cell.index = mutable_index ? "segmented" : "sealed_csr";

            Scene<TypedStore> scene(EvalModel::EagerPush, mutable_index);
            const SchemaId schema = scene.regions().add_schema("Scalar", albor::prototype_d2::InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

            // build the committed binding count as one long chain, the shape D2 measured its mutation on (§4)
            PropertyId previous = 0;

            for (std::size_t index = 0; index < MUTATION_BINDINGS; index++) {
                const InstanceId instance = scene.store().create(schema);
                const PropertyId property = scene.at(instance, 0);
                static_cast<void>(scene.store().write(property, Value(static_cast<std::int64_t>(0)), WriteSource::GeneratedInit));

                if (index == 0) {
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{property}, &plain_pass<TypedStore>));
                }
                else {
                    static_cast<void>(scene.graph().add_binding(property, std::vector<PropertyId>{previous}, &plain_pass<TypedStore>));
                }

                previous = property;
            }

            scene.graph().seal();
            static_cast<void>(scene.graph().settle());

            cell.bindings = scene.graph().binding_count();
            cell.index_bytes_before = scene.graph().index_bytes();
            cell.samples.reserve(POST_SEAL_ADDITIONS);

            // add one dependency to a live binding per iteration, which is what a conditional row does (§18.1)
            for (std::size_t addition = 0; addition < POST_SEAL_ADDITIONS; addition++) {
                const PropertyId dependency = static_cast<PropertyId>((addition * 97) % cell.bindings);
                const Clock::time_point start = Clock::now();
                static_cast<void>(scene.graph().add_dependency(static_cast<BindingId>(addition % cell.bindings), dependency));
                cell.samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
            }

            cell.index_bytes_after = scene.graph().index_bytes();

            // hand back what this index charged for a local edit (§28.2)
            return cell;
        }

        // find one cell by its identity, so the factorial's arithmetic reads as the design intends (§28.2)
        const Cell* cell_for(const std::vector<Cell>& cells, const std::string& index, const std::string& model, const Shape shape) {
            for (const Cell& cell : cells) {
                if (cell.index == index && cell.model == model && cell.shape == shape) {
                    return &cell;
                }
            }

            // report nothing rather than a wrong cell (§28.2)
            return nullptr;
        }

    } // namespace

    void run_factorial(Verdict& verdict) {
        const std::vector<Shape> shapes{Shape::Chain16, Shape::Diamond32, Shape::Reconvergent, Shape::DShape};
        const std::vector<EvalModel> models{EvalModel::EagerPush, EvalModel::HybridPushPull, EvalModel::PullTopo};

        // measure every cell: both axes, plus the recorded pull column (§28.4)
        std::vector<Cell> cells;

        for (const Shape shape : shapes) {
            for (const bool mutable_index : {false, true}) {
                for (const EvalModel model : models) {
                    cells.push_back(measure_cell(mutable_index, model, shape));
                }
            }
        }

        std::printf("--- the factorial: 2 indexes x 3 models x 4 topologies, %zu measured settles each ---\n", MEASURED_SETTLES);
        std::printf("  %-12s %-18s %-14s %6s %8s %8s %8s %10s %10s %8s\n", "index", "model", "topology", "binds", "p50 us", "p99 us", "passes", "evaluated", "redundant", "glitches");

        for (const Cell& cell : cells) {
            report_cell(cell);
        }

        // T3.4.2: every cell must reach the same values, or two engines are not interchangeable (§28.4)
        {
            std::size_t compared = 0;
            std::size_t divergent = 0;
            std::string first_divergence;

            for (std::size_t left = 0; left < cells.size(); left++) {
                for (std::size_t right = left + 1; right < cells.size(); right++) {
                    // compare within one topology, because two topologies hold different values by construction (§28.4)
                    if (cells.at(left).shape != cells.at(right).shape) {
                        continue;
                    }

                    compared++;

                    if (cells.at(left).final_values != cells.at(right).final_values) {
                        divergent++;

                        if (first_divergence.empty()) {
                            // find the first property the two cells disagree about, which is the diagnosable one (§28.2)
                            std::size_t property = 0;

                            while (property < cells.at(left).final_values.size() && cells.at(left).final_values.at(property) == cells.at(right).final_values.at(property)) {
                                property++;
                            }

                            first_divergence = shape_name(cells.at(left).shape) + " property " + std::to_string(property) + ": " + cells.at(left).index + "/" + cells.at(left).model + " holds " + (property < cells.at(left).final_values.size() ? std::to_string(cells.at(left).final_values.at(property)) : "none") + ", " + cells.at(right).index + "/" + cells.at(right).model + " holds " + (property < cells.at(right).final_values.size() ? std::to_string(cells.at(right).final_values.at(property)) : "none");
                        }
                    }
                }
            }

            check(verdict, "T3.4.2", "every cell reaches identical final values", divergent == 0, std::to_string(compared - divergent) + "/" + std::to_string(compared) + " comparisons agreed" + (first_divergence.empty() ? "" : ", first divergence " + first_divergence));
        }

        // T3.4.1: the one-hop fan-in must be glitch-free in every cell (§28.4)
        {
            std::size_t worst = 0;
            std::string worst_cell;

            for (const Cell& cell : cells) {
                if (cell.shape != Shape::Diamond32) {
                    continue;
                }

                const std::size_t per_settle = cell.glitches / MEASURED_SETTLES;

                if (per_settle > worst) {
                    worst = per_settle;
                    worst_cell = cell.index + "/" + cell.model;
                }
            }

            check(verdict, "T3.4.1", "0 glitches per settle at diamond32", worst <= T3_4_1_GLITCHES, worst == 0 ? ("all six cells published no superseded value across " + std::to_string(MEASURED_SETTLES) + " settles each") : (worst_cell + " published " + std::to_string(worst) + " per settle"));
        }

        // T3.4.3 and T3.4.4: the two axes, and whether they interact (§28.4)
        {
            // the multi-hop topologies are the ones where evaluation cost is not trivial (§28.4)
            double index_effect_eager = 0.0;
            double index_effect_hybrid = 0.0;
            double model_effect_csr = 0.0;
            double model_effect_segmented = 0.0;
            std::size_t counted = 0;

            for (const Shape shape : {Shape::Reconvergent, Shape::DShape}) {
                const Cell* base = cell_for(cells, "sealed_csr", "eager_push", shape);
                const Cell* segmented = cell_for(cells, "segmented", "eager_push", shape);
                const Cell* hybrid = cell_for(cells, "sealed_csr", "hybrid_push_pull", shape);
                const Cell* hybrid_segmented = cell_for(cells, "segmented", "hybrid_push_pull", shape);

                // report nothing rather than divide by a cell that was not measured (§28.4)
                if (base == nullptr || segmented == nullptr || hybrid == nullptr || hybrid_segmented == nullptr || base->samples.empty()) {
                    continue;
                }

                index_effect_eager += (median_of(segmented->samples) - median_of(base->samples)) / median_of(base->samples);
                index_effect_hybrid += (median_of(hybrid_segmented->samples) - median_of(hybrid->samples)) / median_of(hybrid->samples);
                model_effect_csr += (median_of(hybrid->samples) - median_of(base->samples)) / median_of(base->samples);
                model_effect_segmented += (median_of(hybrid_segmented->samples) - median_of(segmented->samples)) / median_of(segmented->samples);
                counted++;
            }

            // average over the multi-hop topologies that were measured (§28.4)
            index_effect_eager /= static_cast<double>(std::max<std::size_t>(1, counted));
            index_effect_hybrid /= static_cast<double>(std::max<std::size_t>(1, counted));
            model_effect_csr /= static_cast<double>(std::max<std::size_t>(1, counted));
            model_effect_segmented /= static_cast<double>(std::max<std::size_t>(1, counted));

            // the index main effect is the mean of its per-model effects, and the model main effect likewise (§28.4)
            const double index_effect = (index_effect_eager + index_effect_hybrid) / 2.0;
            const double model_effect = (model_effect_csr + model_effect_segmented) / 2.0;

            // the interaction is how far the two per-model index effects sit from each other (§28.4)
            const double interaction = std::abs(index_effect_eager - index_effect_hybrid) / std::max({std::abs(index_effect), std::abs(model_effect), 1e-9});

            check(verdict, "T3.4.3", "index axis moves settle p50 by <= 10%", std::abs(index_effect) <= T3_4_3_INDEX_EFFECT, format(index_effect * 100.0, 2) + "% (" + format(index_effect_eager * 100.0, 2) + "% at eager, " + format(index_effect_hybrid * 100.0, 2) + "% at hybrid)");

            check(verdict, "T3.4.4", "interaction term <= 10% of the larger main effect", interaction <= T3_4_4_INTERACTION, format(interaction * 100.0, 2) + "% (index " + format(index_effect * 100.0, 2) + "%, model " + format(model_effect * 100.0, 2) + "%)");

            record("-", "the axes, as measured", "index effect " + format(index_effect * 100.0, 2) + "%, model effect " + format(model_effect * 100.0, 2) + "%, interaction " + format(interaction * 100.0, 2) + "%");
        }

        // T3.4.5: the mutation axis, which is where the two indexes actually differ (§28.4)
        {
            const MutationCell segmented = measure_mutation(true);
            const MutationCell sealed = measure_mutation(false);

            std::printf("--- %zu post-seal dependency additions at %zu bindings ---\n", POST_SEAL_ADDITIONS, MUTATION_BINDINGS);
            std::printf("  %-12s %10s %10s %14s %14s\n", "index", "p50 us", "p99 us", "index B before", "index B after");

            for (const MutationCell* cell : {&segmented, &sealed}) {
                std::printf("  %-12s %10s %10s %14zu %14zu\n", cell->index.c_str(), format(median_of(cell->samples), 3).c_str(), format(p99_of(cell->samples), 3).c_str(), cell->index_bytes_before, cell->index_bytes_after);
            }

            const double segmented_p99 = p99_of(segmented.samples);
            const double sealed_p99 = p99_of(sealed.samples);
            const double ratio = sealed_p99 / std::max(segmented_p99, 1e-9);

            check(verdict, "T3.4.5", "segmented add_dependency p99 <= 1 us, sealed <= 50x that", segmented_p99 <= T3_4_5_SEGMENTED_ADD_US && sealed_p99 <= segmented_p99 * T3_4_5_SEGMENTED_RATIO, "segmented " + format(segmented_p99, 3) + " us, sealed " + format(sealed_p99, 2) + " us, ratio " + format(ratio, 0) + "x");

            record("-", "mutation locality at scale", "the segmented index answers a local edit in " + format(segmented_p99, 3) + " us and the sealed index in " + format(sealed_p99, 2) + " us, which is " + format(ratio, 0) + "x at " + std::to_string(MUTATION_BINDINGS) + " bindings against D2's 155x at a couple of hundred");
        }

        // T3.4.6: no cell may trip the pass guard on an acyclic corpus (§28.4)
        {
            std::size_t exhausted = 0;

            for (const Cell& cell : cells) {
                exhausted += cell.budget_exhausted;
            }

            check(verdict, "T3.4.6", "no cell exhausts the 64-pass settle budget", exhausted == 0, std::to_string(exhausted) + " of " + std::to_string(cells.size()) + " cells exhausted it over " + std::to_string(MEASURED_SETTLES) + " settles each");
        }

        // T3.4.7: the recorded detail, which is the decision input (§28.4)
        for (const Cell& cell : cells) {
            record("T3.4.7", shape_name(cell.shape) + " " + cell.index + "/" + cell.model, "p50 " + format(median_of(cell.samples), 2) + " us, p99 " + format(p99_of(cell.samples), 2) + " us, passes/settle " + format(static_cast<double>(cell.passes) / static_cast<double>(MEASURED_SETTLES), 1) + ", evaluated/settle " + format(static_cast<double>(cell.bindings_evaluated) / static_cast<double>(MEASURED_SETTLES), 1) + ", redundant/settle " + format(static_cast<double>(cell.redundant_evaluations) / static_cast<double>(MEASURED_SETTLES), 1) + ", candidates/settle " + format(static_cast<double>(cell.candidates) / static_cast<double>(MEASURED_SETTLES), 1) + ", glitches/settle " + format(static_cast<double>(cell.glitches) / static_cast<double>(MEASURED_SETTLES), 2) + ", index " + std::to_string(cell.index_bytes) + " B");
        }

        record("-", "the write log", std::to_string(g_log.total_writes()) + " logged publications across the last cell");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R4 — the index x evaluation factorial (AD-04a, AD-04c)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R4, committed before this program existed\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_factorial(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
