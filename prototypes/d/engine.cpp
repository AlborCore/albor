#include "prototypes/d/engine.h"

#include <string_view>
#include <utility>

namespace albor::prototype_d
{

    namespace
    {

        // bound the fixpoint passes so that a dependency cycle cannot evaluate forever
        constexpr std::size_t MAX_EVALUATION_PASSES = 64;

    } // namespace

    BindingEngine::BindingEngine(PropertyStore& store, std::unique_ptr<InvalidationStrategy> strategy) :
        m_store(store), m_strategy(std::move(strategy)) {
        // reserve room for the bindings of one evaluation pass
        m_plan.reserve(64);
    }

    BindingEngine::~BindingEngine() = default;

    BindingId BindingEngine::add_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn fn) {
        // prepare an empty binding record
        BindingRecord record;

        // store the plain function pointer instead of a std::function (§13.3)
        record.fn = fn;

        // store the property this binding writes
        record.target = target;

        // point the record at its slice of the flat dependency array (§13.3)
        record.dependency_offset = static_cast<std::uint32_t>(m_declared_dependencies.size());
        record.dependency_count = static_cast<std::uint32_t>(dependencies.size());

        // append the declared dependencies to the shared flat array
        m_declared_dependencies.insert(m_declared_dependencies.end(), dependencies.begin(), dependencies.end());

        // append the record to the binding table
        m_bindings.push_back(record);

        // attach the binding to its property so that an assignment breaks it (§13.4)
        m_store.attach_binding(target);

        // register the binding with the invalidation strategy
        m_strategy->declare(static_cast<BindingId>(m_bindings.size() - 1), dependencies, target);

        // hand back the dense id that identifies the binding
        return static_cast<BindingId>(m_bindings.size() - 1);
    }

    void BindingEngine::seal() {
        // build the index once at the point where every binding is declared (§14.2)
        m_strategy->finish(m_store.size());

        // record that the index is built
        m_sealed = true;

        // replay the invalidations that happened before the index existed
        for (const PropertyId property : m_deferred) {
            m_strategy->invalidate(property);
        }

        // forget the replayed invalidations
        m_deferred.clear();
    }

    bool BindingEngine::set_animation(PropertyId property, Value value) {
        // publish the animated value without breaking the binding (§13.5)
        const bool changed = m_store.write_from_animation(property, std::move(value));

        // schedule the readers only when the animated value changed
        if (changed) {
            invalidate(property);
        }

        // report the change that the frame scheduler has to redraw for
        return changed;
    }

    void BindingEngine::end_animation(PropertyId property) {
        // restore the shadowed binding value (§13.5)
        m_store.end_animation(property);

        // schedule the readers of the restored value
        invalidate(property);
    }

    void BindingEngine::assign(PropertyId property, Value value) {
        // let an imperative assignment win even when the value is equal (§13.4)
        const bool changed = m_store.assign(property, std::move(value));

        // schedule the readers of the property only when the value changed
        if (changed) {
            invalidate(property);
        }
    }

    void BindingEngine::post_set(PropertyId property, Value value) { m_store.post_set(property, std::move(value)); }

    void BindingEngine::invalidate(PropertyId property) {
        // count the invalidation for the throughput measurement (§28.2)
        m_invalidated_total++;

        // defer the invalidation until the strategy index exists
        if (!m_sealed) {
            m_deferred.push_back(property);
            return;
        }

        // hand the property to the strategy
        m_strategy->invalidate(property);
    }

    void BindingEngine::observe_write(void* user, PropertyId property, bool changed) {
        // skip a write that left the value equal (§13.3)
        if (!changed) {
            return;
        }

        // invalidate the readers of the target so that a chain settles (§13.3)
        static_cast<BindingEngine*>(user)->invalidate(property);
    }

    EvaluationStats BindingEngine::evaluate() {
        // accumulate what the passes did for the Prototype D measurements (§30)
        EvaluationStats stats;

        // remember the invalidation count before the worker writes are applied
        const std::size_t invalidated_before = m_invalidated_total;

        // apply the worker writes first so that the pass sees the newest snapshots (§13.2)
        m_drained.clear();
        m_store.drain_inbox(m_drained);

        // schedule the readers of every property a worker wrote
        for (const PropertyId property : m_drained) {
            invalidate(property);
        }

        // ask the strategy for the first pass
        m_plan.clear();
        m_strategy->plan(m_plan);

        // evaluate pass after pass until the values settle (§13.3)
        while (!m_plan.empty()) {
            // stop a cycle that never settles instead of hanging the UI thread
            if (stats.passes >= MAX_EVALUATION_PASSES) {
                stats.cycle_budget_exhausted = true;
                break;
            }

            // count the pass and the bindings it had to consider (§28.2)
            stats.passes++;
            stats.candidates_considered += m_strategy->last_candidate_count();
            stats.bindings_planned += m_plan.size();

            // run the bindings of this pass in the order the strategy produced
            for (const BindingId binding : m_plan) {
                // resolve the record that describes the binding
                const BindingRecord& record = m_bindings.at(binding);

                // skip a binding whose property an explicit write or an animation owns (§13.4, §13.5)
                if (!m_store.has_binding(record.target)) {
                    // count the skipped binding
                    stats.bindings_skipped++;

                    // move to the next binding
                    continue;
                }

                // resolve the declared dependencies of the binding (§13.3)
                const std::span<const PropertyId> dependencies(m_declared_dependencies.data() + record.dependency_offset, record.dependency_count);

                // build the plain context the binding function receives (§13.3)
                BindingContext context(m_store, record.target, binding, dependencies, &BindingEngine::observe_write, this);

                // run the binding, which writes its target through the context
                record.fn(context);

                // count the evaluated binding
                stats.bindings_evaluated++;
            }

            // ask the strategy for the pass that the writes produced
            m_plan.clear();
            m_strategy->plan(m_plan);
        }

        // report how many properties this settle invalidated (§28.2)
        stats.properties_invalidated = m_invalidated_total - invalidated_before;

        // hand back the measurement of this settle (§30)
        return stats;
    }

    void BindingEngine::rebind(BindingId binding) {
        // resolve the property the binding writes
        const PropertyId target = m_bindings.at(binding).target;

        // give the property back to its declarative binding (§13.4)
        m_store.rebind(target);

        // schedule an immediate re-evaluation of the readers (§13.4)
        invalidate(target);
    }

    EngineMemory BindingEngine::memory() const {
        // start an empty memory report
        EngineMemory usage;

        // charge the property slots of the component store
        usage.property_slots = m_store.slot_bytes();

        // charge the payloads the stored values point at
        usage.known_value_heap = m_store.known_heap_bytes();

        // charge the binding records
        usage.binding_records = m_bindings.size() * sizeof(BindingRecord);

        // charge the declared dependencies of every binding
        usage.declared_dependencies = m_declared_dependencies.size() * sizeof(PropertyId);

        // charge whatever the invalidation strategy keeps in memory
        usage.dependency_index = m_strategy->index_bytes();

        // charge the side table that holds the shadowed binding values (§13.5)
        usage.animation_side_table = m_store.animation_side_bytes();

        // hand back the memory report of the prototype (§30)
        return usage;
    }

    std::string_view BindingEngine::strategy_name() const { return m_strategy->name(); }

    std::size_t BindingEngine::invalidated_total() const { return m_invalidated_total; }

    void BindingEngine::reset_invalidated_total() { m_invalidated_total = 0; }

} // namespace albor::prototype_d
