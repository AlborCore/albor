#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"

namespace albor::prototype_d2
{

    // copy the first declared dependency into the target (§13.3)
    template <class Store>
    void copy_first(BindingContext<Store>& context) {
        context.write(context.read(context.declared().front()));
    }

    // add one to the first dependency, so a chain of them never settles (§7)
    template <class Store>
    void increment_first(BindingContext<Store>& context) {
        context.write_int(context.read_int(context.declared().front()) + 1);
    }

    // clamp the first dependency to at most one, which converges even inside a cycle (§7)
    template <class Store>
    void clamp_first(BindingContext<Store>& context) {
        context.write_int(std::min<std::int64_t>(context.read_int(context.declared().front()), 1));
    }

    // clamp the first dependency to at least zero, which converges even inside a cycle (§7)
    template <class Store>
    void floor_first(BindingContext<Store>& context) {
        context.write_int(std::max<std::int64_t>(context.read_int(context.declared().front()), 0));
    }

    // add the first dependency to the second and clamp the result, so a cycle it belongs to converges (§7)
    //
    // this is how a cycle is kicked in a test: an unbound source is one of the declared dependencies, so changing the
    // source invalidates the cycle without an imperative write breaking either binding (§7)
    template <class Store>
    void offset_then_clamp(BindingContext<Store>& context) {
        const std::int64_t total = context.read_int(context.declared()[0]) + context.read_int(context.declared()[1]);
        context.write_int(std::min<std::int64_t>(total, 1));
    }

    // add the first dependency to the second, so a cycle it belongs to never settles (§7)
    template <class Store>
    void offset_then_increment(BindingContext<Store>& context) {
        context.write_int(context.read_int(context.declared()[0]) + context.read_int(context.declared()[1]));
    }

    // sum every declared dependency, so a reconvergent node has several inputs (§3)
    template <class Store>
    void sum_all(BindingContext<Store>& context) {
        std::int64_t total = 0;

        // add every declared dependency (§13.3)
        for (const PropertyId dependency : context.declared()) {
            total += context.read_int(dependency);
        }

        // publish the sum (§13.3)
        context.write_int(total);
    }

    // read an integer and publish it unchanged, standing in for a pass-through binding (§13.3)
    template <class Store>
    void pass_int(BindingContext<Store>& context) {
        context.write_int(context.read_int(context.declared().front()));
    }

    // read an integer and publish the sum with the second dependency, standing in for binding B in a reload (§18.1)
    template <class Store>
    void combine_two(BindingContext<Store>& context) {
        context.write_int(context.read_int(context.declared()[0]) * 10 + context.read_int(context.declared()[1]));
    }

    // read the first dependency as a string and publish it, so a string value flows through a chain (§13.1)
    template <class Store>
    void pass_string(BindingContext<Store>& context) {
        context.write_string(context.read(context.declared().front()).as_string());
    }

    // read the first dependency as a snapshot and publish its sample count (§10.5)
    template <class Store>
    void snapshot_count(BindingContext<Store>& context) {
        context.write_int(static_cast<std::int64_t>(context.read(context.declared().front()).as_snapshot().count()));
    }

    // hold a region table, a store and a graph, in the order the graph needs them (§13.3)
    template <class Store>
    class Scene
    {
    public:
        Scene(EvalModel model, bool mutable_index) :
            m_store(m_regions), m_graph(std::make_unique<Graph<Store>>(m_regions, m_store, model, mutable_index)) {
            // the graph needs the store, so it is created after both members are alive
            static_cast<void>(mutable_index);
        }

        [[nodiscard]] RegionTable& regions() { return m_regions; }
        [[nodiscard]] Store& store() { return m_store; }
        [[nodiscard]] Graph<Store>& graph() { return *m_graph; }

        // resolve a component-local property id through the base offset of the instance (AD-04a)
        [[nodiscard]] PropertyId at(InstanceId instance, LocalPropertyId local) const { return m_regions.global(instance, local); }

    private:
        RegionTable m_regions;
        Store m_store;
        std::unique_ptr<Graph<Store>> m_graph;
    };

    // the property mix a real component declares (§13.1)
    inline std::vector<ValueType> scalar_types(std::size_t count) {
        // cycle through the scalar categories so a layout is representative (§13.1)
        static constexpr ValueType SCALARS[] = {ValueType::Int, ValueType::Double, ValueType::Bool};

        // build the declared types (§13.1)
        std::vector<ValueType> types;
        types.reserve(count);

        for (std::size_t index = 0; index < count; index++) {
            types.push_back(SCALARS[index % 3]);
        }

        // hand back the declared mix
        return types;
    }

    // the mix that includes every payload owning category the design names (§13.1, §23.3)
    inline std::vector<ValueType> realistic_types() {
        // one of each value category, with the scalars repeated to eight properties (§13.1)
        return std::vector<ValueType>{
            ValueType::Int, ValueType::Double, ValueType::Bool, ValueType::String, ValueType::TranslatedString, ValueType::Snapshot, ValueType::Int, ValueType::Double,
        };
    }

    // declare the component types the measurements share (§13.3)
    struct StandardSchemas
    {
        SchemaId scalar8 = 0;
        SchemaId realistic8 = 0;
        SchemaId single_int = 0;
        SchemaId double_int = 0;
    };

    inline StandardSchemas declare_standard_schemas(RegionTable& regions) {
        // declare the schema set the benchmarks address by name (§13.3)
        StandardSchemas schemas;

        // an all-scalar component, for the layout floor of typed storage (§1)
        schemas.scalar8 = regions.add_schema("Scalar8", InstanceKind::Component, scalar_types(8));

        // a component that holds every value category the design names (§13.1)
        schemas.realistic8 = regions.add_schema("Realistic8", InstanceKind::Component, realistic_types());

        // a one property component, used to build graphs node by node (§3)
        schemas.single_int = regions.add_schema("SingleInt", InstanceKind::Component, std::vector<ValueType>{ValueType::Int});

        // a two property component, used for a source and a derived property (§3)
        schemas.double_int = regions.add_schema("DoubleInt", InstanceKind::Component, std::vector<ValueType>{ValueType::Int, ValueType::Int});

        // hand back the declared schemas
        return schemas;
    }

    // build a chain of the given depth, returning the properties in layer order (§2)
    //
    // one source property followed by depth derived properties, each bound to the one before it, so a change at the
    // source has to travel through every layer (§2)
    template <class Store>
    std::vector<PropertyId> build_chain(Scene<Store>& scene, SchemaId schema, std::size_t depth) {
        // resolve the scene handles (§13.3)
        Store& store = scene.store();
        Graph<Store>& graph = scene.graph();

        // collect the properties of the chain in layer order (§2)
        std::vector<PropertyId> chain;
        chain.reserve(depth + 1);

        // create one instance per layer and keep its single property (§2)
        for (std::size_t layer = 0; layer <= depth; layer++) {
            // allocate the instance that owns this layer's property (§3)
            const InstanceId instance = store.create(schema);

            // resolve its property through the base offset (AD-04a)
            chain.push_back(scene.at(instance, 0));

            // seed the property so a chain that copies sees a value (§13.4)
            store.write(scene.at(instance, 0), Value(std::int64_t{0}), WriteSource::GeneratedInit);
        }

        // bind every layer to the one before it, so a source change walks the whole chain (§2)
        for (std::size_t layer = 1; layer <= depth; layer++) {
            // declare the single dependency of this layer (§13.3)
            const std::vector<PropertyId> dependencies{chain.at(layer - 1)};

            // bind the layer to its predecessor (§13.3)
            graph.add_binding(chain.at(layer), dependencies, &pass_int<Store>);
        }

        // hand back the chain so the benchmark can drive its source (§2)
        return chain;
    }

    // build a simple or wide diamond: one source, width intermediates, one sink (§3)
    template <class Store>
    struct DiamondHandles
    {
        PropertyId source = INVALID_PROPERTY;
        std::vector<PropertyId> intermediates;
        PropertyId sink = INVALID_PROPERTY;
    };

    template <class Store>
    DiamondHandles<Store> build_diamond(Scene<Store>& scene, SchemaId schema, std::size_t width) {
        // resolve the scene handles (§13.3)
        Store& store = scene.store();
        Graph<Store>& graph = scene.graph();

        // collect the handles of the diamond (§3)
        DiamondHandles<Store> handles;

        // create the source property every branch reads (§3)
        const InstanceId source_instance = store.create(schema);
        handles.source = scene.at(source_instance, 0);
        store.write(handles.source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

        // create the intermediates, each bound to the source alone (§3)
        for (std::size_t branch = 0; branch < width; branch++) {
            // allocate the intermediate property (§3)
            const InstanceId instance = store.create(schema);
            const PropertyId property = scene.at(instance, 0);
            handles.intermediates.push_back(property);
            store.write(property, Value(std::int64_t{0}), WriteSource::GeneratedInit);

            // bind the intermediate to the source (§13.3)
            graph.add_binding(property, std::vector<PropertyId>{handles.source}, &pass_int<Store>);
        }

        // create the sink that reconverges every branch (§3)
        const InstanceId sink_instance = store.create(schema);
        handles.sink = scene.at(sink_instance, 0);
        store.write(handles.sink, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        // bind the sink to every intermediate, so one binding reads the whole fan-in (§13.3)
        graph.add_binding(handles.sink, handles.intermediates, &sum_all<Store>);

        // hand back the handles of the diamond (§3)
        return handles;
    }

    // build a layered reconvergent graph, where the sink is reachable at several different depths (§3)
    //
    // this is the topology Prototype D never ran: the sink declares a branch that is one hop from the source and a
    // branch that is depth hops away, so an eager push evaluates the sink once per layer the deep branch advances (§3)
    template <class Store>
    DiamondHandles<Store> build_reconvergent(Scene<Store>& scene, SchemaId schema, std::size_t width, std::size_t depth) {
        // resolve the scene handles (§13.3)
        Store& store = scene.store();
        Graph<Store>& graph = scene.graph();

        // collect the handles of the graph (§3)
        DiamondHandles<Store> handles;

        // create the source every branch starts from (§3)
        const InstanceId source_instance = store.create(schema);
        handles.source = scene.at(source_instance, 0);
        store.write(handles.source, Value(std::int64_t{1}), WriteSource::GeneratedInit);

        // create the short branch, which is one hop from the source (§3)
        PropertyId long_tip = handles.source;

        if (depth > 0) {
            // allocate the first property of the deep branch (§3)
            const InstanceId instance = store.create(schema);
            long_tip = scene.at(instance, 0);
            store.write(long_tip, Value(std::int64_t{0}), WriteSource::GeneratedInit);

            // bind the deep branch to the source (§13.3)
            graph.add_binding(long_tip, std::vector<PropertyId>{handles.source}, &pass_int<Store>);

            // extend the deep branch to the requested depth, one binding per layer (§3)
            for (std::size_t layer = 1; layer < depth; layer++) {
                // allocate the next property of the deep branch (§3)
                const InstanceId next_instance = store.create(schema);
                const PropertyId next = scene.at(next_instance, 0);
                store.write(next, Value(std::int64_t{0}), WriteSource::GeneratedInit);

                // bind the layer to the previous one (§13.3)
                graph.add_binding(next, std::vector<PropertyId>{long_tip}, &pass_int<Store>);

                // move the tip down the branch (§3)
                long_tip = next;
            }
        }

        // collect the short branches, which stay one hop from the source (§3)
        for (std::size_t branch = 0; branch < width; branch++) {
            // allocate the short branch property (§3)
            const InstanceId instance = store.create(schema);
            const PropertyId property = scene.at(instance, 0);
            store.write(property, Value(std::int64_t{0}), WriteSource::GeneratedInit);

            // bind the short branch to the source (§13.3)
            graph.add_binding(property, std::vector<PropertyId>{handles.source}, &pass_int<Store>);

            // remember it as an intermediate of the graph (§3)
            handles.intermediates.push_back(property);
        }

        // create the sink that both branches reach (§3)
        const InstanceId sink_instance = store.create(schema);
        handles.sink = scene.at(sink_instance, 0);
        store.write(handles.sink, Value(std::int64_t{0}), WriteSource::GeneratedInit);

        // declare the sink's dependencies as the short branches plus the tip of the deep branch (§3)
        std::vector<PropertyId> sink_dependencies = handles.intermediates;
        sink_dependencies.push_back(long_tip);

        // bind the sink to every branch, which is what makes it reconvergent (§3)
        graph.add_binding(handles.sink, sink_dependencies, &sum_all<Store>);

        // hand back the handles of the graph (§3)
        return handles;
    }

    // build the node shape Prototype D used, so the pass count can be compared to its recorded 8 (§2 T2.5)
    //
    // D gave every node one input, a chain of seven derived properties and three siblings reading the chain tip. The
    // chain tip sits seven bindings below the input, so an input update needs seven passes to reach it and one more
    // pass to discover that nothing is left (§2)
    template <class Store>
    struct DShapeHandles
    {
        std::vector<PropertyId> inputs;
        std::vector<PropertyId> chain_tips;
        std::size_t bound_properties = 0;
    };

    template <class Store>
    DShapeHandles<Store> build_d_shape(Scene<Store>& scene, SchemaId schema, std::size_t node_count, std::size_t chain_layers) {
        // resolve the scene handles (§13.3)
        Store& store = scene.store();
        Graph<Store>& graph = scene.graph();

        // collect the handles of the shape (§2)
        DShapeHandles<Store> handles;

        // build every node of the shape (§2)
        for (std::size_t node = 0; node < node_count; node++) {
            // create the input property of this node (§2)
            const InstanceId input_instance = store.create(schema);
            const PropertyId input = scene.at(input_instance, 0);
            store.write(input, Value(static_cast<std::int64_t>(node)), WriteSource::GeneratedInit);
            handles.inputs.push_back(input);
            handles.bound_properties++;

            // create the first chain property and bind it to the input (§13.3)
            const InstanceId first_instance = store.create(schema);
            PropertyId tip = scene.at(first_instance, 0);
            store.write(tip, Value(std::int64_t{0}), WriteSource::GeneratedInit);
            handles.bound_properties++;

            graph.add_binding(tip, std::vector<PropertyId>{input}, &pass_int<Store>);

            // add the remaining chain layers, so a change needs several passes (§2)
            for (std::size_t layer = 1; layer < chain_layers; layer++) {
                // create the next chain property (§2)
                const InstanceId instance = store.create(schema);
                const PropertyId next = scene.at(instance, 0);
                store.write(next, Value(std::int64_t{0}), WriteSource::GeneratedInit);
                handles.bound_properties++;

                // bind the layer to the previous tip (§13.3)
                graph.add_binding(next, std::vector<PropertyId>{tip}, &pass_int<Store>);

                // move the tip down the chain (§2)
                tip = next;
            }

            // add the three siblings that share the chain tip as their dependency set (§2)
            for (std::size_t sibling = 0; sibling < 3; sibling++) {
                // create the sibling property (§2)
                const InstanceId instance = store.create(schema);
                const PropertyId derived = scene.at(instance, 0);
                store.write(derived, Value(std::int64_t{0}), WriteSource::GeneratedInit);
                handles.bound_properties++;

                // bind the sibling to the chain tip (§13.3)
                graph.add_binding(derived, std::vector<PropertyId>{tip}, &pass_int<Store>);
            }

            // remember the tip so the benchmark can check a derived value (§2)
            handles.chain_tips.push_back(tip);
        }

        // hand back the handles of the shape (§2)
        return handles;
    }

} // namespace albor::prototype_d2
