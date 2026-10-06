#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "prototypes/d/binding.h"
#include "prototypes/d/property_id.h"
#include "prototypes/d/store.h"
#include "prototypes/d/strategy.h"
#include "prototypes/d/value.h"

namespace albor::prototype_d
{

    // collect what one evaluate call did to feed the Prototype D measurements (§30)
    struct EvaluationStats
    {
        std::size_t passes = 0;
        std::size_t bindings_evaluated = 0;
        std::size_t bindings_skipped = 0;
        std::size_t bindings_planned = 0;
        std::size_t candidates_considered = 0;
        std::size_t properties_invalidated = 0;
        bool cycle_budget_exhausted = false;
    };

    // split the memory into property storage and dependency index costs
    struct EngineMemory
    {
        std::size_t property_slots = 0;
        std::size_t known_value_heap = 0;
        std::size_t binding_records = 0;
        std::size_t declared_dependencies = 0;
        std::size_t dependency_index = 0;
        std::size_t animation_side_table = 0;
    };

    // hold a plain function pointer, the written property and a dependency slice (§13.3)
    struct BindingRecord
    {
        BindingFn fn = nullptr;
        PropertyId target = INVALID_PROPERTY;
        std::uint32_t dependency_offset = 0;
        std::uint32_t dependency_count = 0;
    };

    // own the binding table, drive a strategy and evaluate to a fixpoint
    class BindingEngine
    {
    public:
        explicit BindingEngine(PropertyStore& store, std::unique_ptr<InvalidationStrategy> strategy);
        ~BindingEngine();

        BindingEngine(const BindingEngine&) = delete;
        BindingEngine& operator=(const BindingEngine&) = delete;
        BindingEngine(BindingEngine&&) = delete;
        BindingEngine& operator=(BindingEngine&&) = delete;

        // register a plain binding function with its declared dependencies (§13.3)
        BindingId add_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn fn);

        // build the index once every binding is declared (§13.4)
        void seal();

        // break the owning binding on an imperative assignment (§13.4)
        void assign(PropertyId property, Value value);

        // queue an assignment from any thread for evaluate (§13.2)
        void post_set(PropertyId property, Value value);

        // write from the animation layer, which yields to an explicit write (§13.5)
        bool set_animation(PropertyId property, Value value);

        // end the animation and let the shadowed binding value come back (§13.5)
        void end_animation(PropertyId property);

        // apply the queued worker writes and evaluate until the values settle
        EvaluationStats evaluate();

        // report how many properties the engine invalidated since the last reset (§28.2)
        [[nodiscard]] std::size_t invalidated_total() const;

        // reset the invalidation counter before a measured window (§28.2)
        void reset_invalidated_total();

        // restore a binding broken by an imperative assignment (§13.4)
        void rebind(BindingId binding);

        [[nodiscard]] const PropertyStore& store() const { return m_store; }
        [[nodiscard]] std::size_t binding_count() const { return m_bindings.size(); }
        [[nodiscard]] std::vector<BindingId> last_plan() const { return m_plan; }
        [[nodiscard]] bool sealed() const { return m_sealed; }
        [[nodiscard]] EngineMemory memory() const;
        [[nodiscard]] std::string_view strategy_name() const;

    private:
        // receive the write notification from a binding context
        static void observe_write(void* user, PropertyId property, bool changed);

        // schedule a changed property and count it for the throughput measurement (§28.2)
        void invalidate(PropertyId property);

        PropertyStore& m_store;
        std::unique_ptr<InvalidationStrategy> m_strategy;
        std::vector<BindingRecord> m_bindings;
        std::vector<PropertyId> m_declared_dependencies;
        std::vector<PropertyId> m_drained;
        std::vector<PropertyId> m_deferred;
        std::vector<BindingId> m_plan;
        std::size_t m_invalidated_total = 0;
        bool m_sealed = false;
    };

} // namespace albor::prototype_d
