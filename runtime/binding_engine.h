#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "runtime/property_id.h"

namespace albor
{
    // the identifier of a registered binding, where zero names no binding
    using BindingId = std::uint64_t;

    // the work a binding runs to produce the value of one property (§7.3)
    using BindingEvaluator = std::function<void()>;

    // the work a static binding runs, which carries its context as a plain pointer instead of capturing one (§13.3)
    using StaticBindingFn = void (*)(void* context);

    // the packed key a dependency map indexes on, which keeps two components from colliding on a shared slot (§7.2, §7.3)
    [[nodiscard]] constexpr std::uint64_t property_key(PropertyId id) { return (static_cast<std::uint64_t>(id.base) << 32) | static_cast<std::uint64_t>(id.slot); }

    // the eager push engine that propagates property changes along the dependency graph (§7.3, AD-04a, AD-04c)
    class BindingEngine
    {
    public:
        // the largest number of evaluation passes a settle may make before it declares a cycle (§7.3)
        static constexpr std::size_t EVALUATION_BUDGET = 64;

        // build an engine over no components, which begin_component then scopes
        BindingEngine() = default;

        BindingEngine(const BindingEngine&) = delete;
        BindingEngine& operator=(const BindingEngine&) = delete;
        BindingEngine(BindingEngine&&) = delete;
        BindingEngine& operator=(BindingEngine&&) = delete;
        ~BindingEngine() = default;

        // begin a component and hand out the base offset every property of it shares (§7.2, AD-04b)
        [[nodiscard]] std::uint32_t begin_component();

        // hand out the next property slot of the component that is currently open
        [[nodiscard]] PropertyId next_property_id();

        // install an evaluator that writes the target property from its declared dependencies (§7.3)
        [[nodiscard]] BindingId bind(PropertyId target, std::vector<PropertyId> dependencies, BindingEvaluator evaluator);

        // install a static evaluator over caller-owned dependencies, which generated code registers without allocating (§13.3)
        [[nodiscard]] BindingId bind_static(PropertyId target, std::span<const PropertyId> dependencies, StaticBindingFn evaluator, void* context);

        // remove a binding, which stops the engine evaluating it (§7.3)
        bool unbind(BindingId binding);

        // report whether a handle still names a registered binding
        [[nodiscard]] bool bound(BindingId binding) const;

        // hand out the target property a binding writes
        [[nodiscard]] PropertyId target(BindingId binding) const;

        // hand out the declared dependencies of a binding, in the order they were declared
        [[nodiscard]] const std::vector<PropertyId>& dependencies(BindingId binding) const;

        // report how many bindings depend on a property, which a test asserts on
        [[nodiscard]] std::size_t dependent_count(PropertyId source) const;

        // hand out the number of bindings that are still registered
        [[nodiscard]] std::size_t size() const;

        // mark every binding that depends on a property dirty, which a property write calls (§7.3)
        void notify_changed(PropertyId source);

        // mark one binding dirty without a property change, which an animation presence write calls (AD-04d)
        void invalidate(BindingId binding);

        // evaluate the dirty bindings in topological order until none remain or the budget is spent (§7.3, AD-04c)
        std::size_t settle();

        // hand out how many bindings are waiting to be evaluated
        [[nodiscard]] std::size_t dirty_count() const;

        // hand out how many evaluations the engine has performed, which a test asserts on
        [[nodiscard]] std::size_t evaluation_count() const;

        // report whether a settle spent its whole budget, which is the dynamic cycle diagnostic (§7.3)
        [[nodiscard]] bool cycle_detected() const;

        // forget the cycle diagnostic, which a test clears between cases
        void clear_cycle();

    private:
        // one registered binding with the sources it reads and the work it runs
        struct Binding
        {
            // the property the evaluator writes
            PropertyId target;

            // the properties the evaluator reads, in declaration order
            std::vector<PropertyId> dependencies;

            // the work that produces the bound value
            BindingEvaluator evaluator;

            // the static work that runs when the binding installed a plain function instead of a callable (§13.3)
            StaticBindingFn static_evaluator = nullptr;

            // the context the static work reads, which it cannot capture on its own (§13.3)
            void* static_context = nullptr;

            // report whether the binding still carries work, which is what a live handle means (§7.3)
            [[nodiscard]] bool registered() const { return evaluator != nullptr || static_evaluator != nullptr; }

            // whether the binding is waiting to be evaluated
            bool dirty = false;

            // whether the binding sits on the current topological walk, which finds a cycle (§7.3)
            bool visiting = false;

            // whether the binding already took its place in the order of the current pass (§7.3, AD-04a)
            bool ordered = false;
        };

        // find a binding by handle, returning null when the handle is stale
        [[nodiscard]] Binding* find(BindingId binding);

        // find a binding by handle for reading, returning null when the handle is stale
        [[nodiscard]] const Binding* find(BindingId binding) const;

        // hand out the handle of the binding that writes a property, or zero when nothing does
        [[nodiscard]] BindingId producer_of(PropertyId target) const;

        // walk a binding depth first so the returned order evaluates sources before consumers (§7.3, AD-04a)
        void visit(BindingId binding, std::vector<BindingId>& order);

        // the registered bindings
        std::vector<Binding> m_bindings;

        // the bindings that read each source property, which is the reverse edge of the graph (§7.3)
        std::unordered_map<std::uint64_t, std::vector<BindingId>> m_dependents;

        // the binding that writes each target property, which is the forward edge of the graph (§7.3)
        std::unordered_map<std::uint64_t, BindingId> m_producers;

        // the allocator that hands out component scoped property identifiers (§7.2)
        PropertyIdAllocator m_allocator;

        // the handle the next registered binding receives, where zero is never handed out
        BindingId m_next_binding = 1;

        // how many evaluations the engine has performed
        std::size_t m_evaluations = 0;

        // whether a settle spent its whole budget without converging (§7.3)
        bool m_cycle_detected = false;
    };
} // namespace albor
