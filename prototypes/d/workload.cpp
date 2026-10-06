#include "prototypes/d/workload.h"

#include <utility>
#include <vector>

namespace albor::prototype_d
{

    namespace
    {

        // use generic binding functions that measure like per binding generated code (§13.3)
        void accumulate_binding(BindingContext& context) {
            // start the accumulator at zero
            double sum = 0.0;

            // add every declared dependency to the accumulator
            for (const PropertyId dependency : context.declared()) {
                sum += context.read_double(dependency);
            }

            // one arithmetic step per dependency keeps the evaluation cost proportional to the declared dependency count
            context.write_double(sum * 1.5 - 0.25);
        }

        void enabled_binding(BindingContext& context) {
            // read the first declared dependency as the enabled source
            const double value = context.read_double(context.declared().front());

            // publish the derived enabled flag
            context.write_bool(value > 0.0);
        }

        void snapshot_extent_binding(BindingContext& context) {
            // read the snapshot the worker published (§10.5)
            const SnapshotBox snapshot = context.read(context.declared().front()).as_snapshot();

            // publish the sample count the application can bind against
            context.write_int(static_cast<std::int64_t>(snapshot.count()));
        }

    } // namespace

    std::shared_ptr<const std::vector<double>> make_snapshot(std::size_t sample_count) {
        // the payload is built once and then shared as immutable const storage (§10.5)
        return std::make_shared<const std::vector<double>>(sample_count, 0.0);
    }

    WorkloadScene build_scene(const WorkloadShape& shape, std::unique_ptr<InvalidationStrategy> strategy) {
        // start the scene that the benchmark drives
        WorkloadScene scene;

        // give the scene its own property store
        scene.store = std::make_unique<PropertyStore>();

        // build the engine on top of that store and the candidate strategy
        scene.engine = std::make_unique<BindingEngine>(*scene.store, std::move(strategy));

        // resolve the store and the engine for the setup below
        PropertyStore& store = *scene.store;
        BindingEngine& engine = *scene.engine;

        // add the environment properties that bindings depend on like any other (§13.1)
        scene.handles.theme = store.add(Value(std::int64_t{1}));
        scene.handles.locale = store.add(Value(std::int64_t{0}));

        // collect the environment ids the bindings declare
        std::vector<PropertyId> environment;
        environment.push_back(scene.handles.theme);
        environment.push_back(scene.handles.locale);

        // add the remaining environment properties of the fixed shape (§30)
        for (std::size_t index = 2; index < shape.environment_properties; index++) {
            environment.push_back(store.add(Value(std::int64_t{static_cast<std::int64_t>(index)})));
        }

        // add the immutable properties that a worker publishes (§10.5)
        for (std::size_t index = 0; index < shape.snapshot_properties; index++) {
            // build one immutable payload standing in for application data
            const SnapshotBox box(ValueType::Double, std::shared_ptr<const void>(make_snapshot(1024)), 1024);

            // store the snapshot as an observable property
            scene.handles.snapshots.push_back(store.add(Value(box)));
        }

        // derive the layer count a node has before its shared siblings
        const std::size_t chain_layers = shape.chain_depth > 2 ? shape.chain_depth - 1 : 1;

        // count the bindings every node declares
        const std::size_t bindings_per_node = chain_layers + 3;

        // derive how many nodes reach the declared binding count (§30)
        const std::size_t node_count = std::max<std::size_t>(1, shape.binding_count / bindings_per_node);

        // start the property accounting with the properties added so far
        std::size_t used_properties = environment.size() + shape.snapshot_properties;

        // build every node of the fixed shape
        for (std::size_t node = 0; node < node_count; node++) {
            // give every node an input, a chain of derived properties and shared siblings
            const PropertyId input = store.add(Value(static_cast<double>(node)));
            scene.handles.inputs.push_back(input);

            // count the input property
            used_properties++;

            // declare the inputs of the first chain layer
            std::vector<PropertyId> dependencies;
            dependencies.reserve(3);
            dependencies.push_back(input);
            dependencies.push_back(environment.front());
            dependencies.push_back(environment.at(1));

            // add the property the first layer writes
            PropertyId tip = store.add(Value(0.0));

            // count the first chain property
            used_properties++;

            // bind the first layer to its declared inputs (§13.3)
            engine.add_binding(tip, dependencies, &accumulate_binding);

            // add the remaining chain layers so that a change needs several passes
            for (std::size_t layer = 1; layer < chain_layers; layer++) {
                // point the next layer at the previous tip
                dependencies[0] = tip;

                // add the property the next layer writes
                const PropertyId next = store.add(Value(0.0));

                // count the chain property
                used_properties++;

                // bind the next layer to the previous tip
                engine.add_binding(next, dependencies, &accumulate_binding);

                // move the tip to the new property
                tip = next;
            }

            // add three siblings that read the same properties to exercise node sharing
            std::vector<PropertyId> shared_dependencies;
            shared_dependencies.push_back(tip);
            shared_dependencies.push_back(environment.front());
            shared_dependencies.push_back(environment.at(1));

            // bind the three siblings that share one dependency set
            for (std::size_t sibling = 0; sibling < 3; sibling++) {
                // add the property the sibling writes
                const PropertyId derived = store.add(Value(0.0));

                // count the sibling property
                used_properties++;

                // bind the sibling, mixing binding shapes at the same dependency set
                engine.add_binding(derived, shared_dependencies, sibling == 0 ? &accumulate_binding : (sibling == 1 ? &enabled_binding : &accumulate_binding));
            }

            // remember the tip so the benchmark can follow a derived value
            scene.handles.tail_outputs.push_back(tip);
        }

        // add the properties the animation layer drives every frame (§13.5)
        for (std::size_t index = 0; index < shape.animated_properties; index++) {
            // give every animated property a binding so the animation has something to shadow
            const PropertyId animated = store.add(Value(0.0));

            // count the animated property
            used_properties++;

            // bind the animated property to the environment inputs (§13.3)
            const std::vector<PropertyId> dependencies{environment.front(), environment.at(1)};
            engine.add_binding(animated, dependencies, &enabled_binding);

            // remember the animated property so the benchmark can drive it
            scene.handles.animated.push_back(animated);
        }

        // feed every snapshot property into one derived property
        for (const PropertyId snapshot : scene.handles.snapshots) {
            // add the property that holds the derived extent
            const PropertyId extent = store.add(Value(std::int64_t{0}));

            // count the extent property
            used_properties++;

            // declare the snapshot as the only input of the extent binding
            const std::vector<PropertyId> dependencies{snapshot};

            // bind the extent to the published snapshot
            engine.add_binding(extent, dependencies, &snapshot_extent_binding);
        }

        // leave the remaining properties without a binding as in a real component tree
        for (std::size_t index = used_properties; index < shape.property_count; index++) {
            // add a property that no binding reads
            const PropertyId unbound = store.add(Value(std::int64_t{static_cast<std::int64_t>(index)}));

            // the id of an unbound property is not used by the benchmark
            static_cast<void>(unbound);
        }

        // build the dependency index now that every binding is declared (§14.2)
        engine.seal();

        // hand back the ready scene
        return scene;
    }

    std::size_t advance_animation(WorkloadScene& scene, std::uint64_t frame) {
        // count the animated values that actually changed
        std::size_t changed = 0;

        // write one frame of every animated property (§13.5)
        for (std::size_t index = 0; index < scene.handles.animated.size(); index++) {
            // oscillate the animated value so that every frame is a real change
            const double value = static_cast<double>((frame + index) % 2);

            // publish the animated value through the animation layer
            changed += scene.engine->set_animation(scene.handles.animated.at(index), Value(value)) ? 1 : 0;
        }

        // hand back how many animated values changed in this frame
        return changed;
    }

    void publish_snapshot(WorkloadScene& scene, std::shared_ptr<const std::vector<double>> samples) {
        // publish immutable storage from the worker and swap it in evaluate (§10.5, §13.2)
        const SnapshotBox box(ValueType::Double, std::shared_ptr<const void>(std::move(samples)), 0);

        // queue the write so that the UI thread applies it (§13.2)
        scene.engine->post_set(scene.handles.snapshots.front(), Value(box));
    }

} // namespace albor::prototype_d
