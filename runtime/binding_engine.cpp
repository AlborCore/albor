#include "runtime/binding_engine.h"

namespace albor
{
    PropertyIdAllocator::PropertyIdAllocator(std::uint32_t first_base) :
        m_base(0), m_next_base(first_base), m_next_slot(0), m_components(0) {}

    std::uint32_t PropertyIdAllocator::begin_component() {
        // the new component takes the next base, which keeps two components off the same slot numbers (§7.2, AD-04b)
        m_base = m_next_base++;
        // a fresh component restarts its slots at zero (§7.2)
        m_next_slot = 0;
        m_components++;
        return m_base;
    }

    PropertyId PropertyIdAllocator::next() {
        // the identifier pairs the open component base with the next slot it hands out (§7.2, AD-04b)
        return PropertyId{.base = m_base, .slot = m_next_slot++};
    }

    std::uint32_t PropertyIdAllocator::component_count() const {
        // the component count is a plain count of how many begin_component calls returned (§7.2)
        return m_components;
    }

    std::uint32_t PropertyIdAllocator::slot_count() const {
        // the slot count is how many properties the open component took so far (§7.2)
        return m_next_slot;
    }

    std::uint32_t BindingEngine::begin_component() {
        // the engine scopes the identifiers of the new component so two components never collide (§7.2)
        return m_allocator.begin_component();
    }

    PropertyId BindingEngine::next_property_id() {
        // the identifier comes from the same allocator that scopes component bases (§7.2)
        return m_allocator.next();
    }

    BindingId BindingEngine::bind(PropertyId target, std::vector<PropertyId> dependencies, BindingEvaluator evaluator) {
        // a target may only carry one binding, because a second one would fight over the same property (§7.3)
        const std::uint64_t key = property_key(target);
        const auto existing = m_producers.find(key);
        if (existing != m_producers.end())
            unbind(existing->second);
        // the handle is the 1-based index, so zero is always free to mean no binding (§7.3)
        const BindingId handle = m_next_binding++;
        Binding binding;
        binding.target = target;
        binding.dependencies = std::move(dependencies);
        binding.evaluator = std::move(evaluator);
        // a fresh binding settles on the next settle, which is what makes the first frame correct (§7.3)
        binding.dirty = true;
        m_bindings.push_back(std::move(binding));
        // every declared dependency gains this binding as a consumer, which is the reverse edge of the graph (§7.3)
        for (const PropertyId dependency : m_bindings.back().dependencies)
            m_dependents[property_key(dependency)].push_back(handle);
        // the target records its writer, which is what orders a consumer after its source (§7.3, AD-04a)
        m_producers[key] = handle;
        return handle;
    }

    bool BindingEngine::unbind(BindingId binding) {
        // a stale handle names nothing, so the caller learns the removal did not happen (§7.3)
        if (binding == 0 || binding > m_bindings.size())
            return false;
        Binding& entry = m_bindings[binding - 1];
        // an already unbound handle changes nothing, so a double removal is a no operation (§7.3)
        if (entry.evaluator == nullptr)
            return false;
        // every dependency loses the consumer edge, so a later change no longer reaches it (§7.3)
        for (const PropertyId dependency : entry.dependencies) {
            auto& consumers = m_dependents[property_key(dependency)];
            // the edge is erased by value, because the handle alone does not identify the binding (§7.3)
            for (auto position = consumers.begin(); position != consumers.end(); ++position)
                if (*position == binding) {
                    consumers.erase(position);
                    break;
                }
        }
        // the producer edge goes with it, so a consumer no longer waits for a dead writer (§7.3)
        const std::uint64_t key = property_key(entry.target);
        const auto producer = m_producers.find(key);
        if (producer != m_producers.end() && producer->second == binding)
            m_producers.erase(producer);
        // the binding is emptied rather than erased, so no handle ever names a different binding (§7.3)
        entry.evaluator = nullptr;
        entry.dependencies.clear();
        entry.dirty = false;
        return true;
    }

    bool BindingEngine::bound(BindingId binding) const {
        // an unbound handle keeps its slot but carries no evaluator (§7.3)
        return binding != 0 && binding <= m_bindings.size() && m_bindings[binding - 1].evaluator != nullptr;
    }

    PropertyId BindingEngine::target(BindingId binding) const {
        const Binding* entry = find(binding);
        // a stale handle names no target (§7.3)
        if (entry == nullptr)
            return invalid_property_id();
        return entry->target;
    }

    const std::vector<PropertyId>& BindingEngine::dependencies(BindingId binding) const {
        // the static empty list answers for a stale handle, so the accessor never hands out a dangling reference (§7.3)
        static const std::vector<PropertyId> empty;
        const Binding* entry = find(binding);
        if (entry == nullptr)
            return empty;
        return entry->dependencies;
    }

    std::size_t BindingEngine::dependent_count(PropertyId source) const {
        const auto found = m_dependents.find(property_key(source));
        // a property nothing depends on has no consumer edge (§7.3)
        if (found == m_dependents.end())
            return 0;
        return found->second.size();
    }

    std::size_t BindingEngine::size() const {
        std::size_t count = 0;
        for (const Binding& binding : m_bindings)
            if (binding.evaluator != nullptr)
                count++;
        return count;
    }

    void BindingEngine::notify_changed(PropertyId source) {
        const auto found = m_dependents.find(property_key(source));
        // a property nothing depends on settles nothing, which is the common case of a leaf (§7.3)
        if (found == m_dependents.end())
            return;
        // every consumer becomes dirty, which is the eager push of the change (§7.3, AD-04c)
        for (const BindingId binding : found->second) {
            Binding* entry = find(binding);
            // an unbound consumer was removed, so it is skipped rather than resurrected (§7.3)
            if (entry != nullptr)
                entry->dirty = true;
        }
    }

    void BindingEngine::invalidate(BindingId binding) {
        Binding* entry = find(binding);
        // a stale handle marks nothing (§7.3)
        if (entry == nullptr)
            return;
        // an animation presence write marks its own binding without walking the graph (AD-04d)
        entry->dirty = true;
    }

    std::size_t BindingEngine::settle() {
        std::size_t passes = 0;
        std::vector<BindingId> order;
        // the engine repeats until the graph converges or the cycle budget is spent (§7.3, AD-04c)
        while (passes < EVALUATION_BUDGET) {
            order.clear();
            // the ordered marks clear each pass, so a producer still precedes its consumer in every pass (§7.3, AD-04a)
            for (Binding& binding : m_bindings)
                binding.ordered = false;
            // every dirty binding enters the walk, which orders sources before their consumers (§7.3, AD-04a)
            for (std::size_t index = 0; index < m_bindings.size(); index++)
                if (m_bindings[index].dirty)
                    visit(static_cast<BindingId>(index + 1), order);
            // a converged graph leaves nothing dirty, so an empty walk ends the settle (§7.3)
            if (order.empty())
                break;
            for (const BindingId binding : order) {
                Binding* entry = find(binding);
                // an unbound binding that a previous pass removed is skipped (§7.3)
                if (entry == nullptr)
                    continue;
                // the dirty flag clears before the run, so a binding that reads its own target still converges (§7.3)
                entry->dirty = false;
                entry->evaluator();
                m_evaluations++;
            }
            passes++;
        }
        // spending the whole budget while work remains is the dynamic cycle diagnostic (§7.3)
        m_cycle_detected = dirty_count() > 0;
        return passes;
    }

    std::size_t BindingEngine::dirty_count() const {
        std::size_t count = 0;
        for (const Binding& binding : m_bindings) {
            if (binding.dirty)
                count++;
        }
        return count;
    }

    std::size_t BindingEngine::evaluation_count() const { return m_evaluations; }

    bool BindingEngine::cycle_detected() const { return m_cycle_detected; }

    void BindingEngine::clear_cycle() {
        // the diagnostic belongs to one settle, so a later case starts from a clean report (§7.3)
        m_cycle_detected = false;
    }

    BindingEngine::Binding* BindingEngine::find(BindingId binding) {
        // handle zero and a handle past the end both name no binding (§7.3)
        if (binding == 0 || binding > m_bindings.size())
            return nullptr;
        return &m_bindings[binding - 1];
    }

    const BindingEngine::Binding* BindingEngine::find(BindingId binding) const {
        // handle zero and a handle past the end both name no binding (§7.3)
        if (binding == 0 || binding > m_bindings.size())
            return nullptr;
        return &m_bindings[binding - 1];
    }

    BindingId BindingEngine::producer_of(PropertyId target) const {
        const auto found = m_producers.find(property_key(target));
        // nothing writes the property, so a consumer of it has no source to wait for (§7.3)
        if (found == m_producers.end())
            return 0;
        return found->second;
    }

    void BindingEngine::visit(BindingId binding, std::vector<BindingId>& order) {
        Binding* entry = find(binding);
        // a stale handle contributes nothing to the order (§7.3)
        if (entry == nullptr)
            return;
        // a binding that already took its place is not walked twice, which is what keeps a diamond single pass (§7.3, AD-04a)
        if (entry->ordered)
            return;
        // a binding already on the walk closes a cycle, so it is left for the budget to flag (§7.3)
        if (entry->visiting) {
            m_cycle_detected = true;
            return;
        }
        entry->visiting = true;
        // the writer of a dependency evaluates first, which is what makes the returned order topological (§7.3, AD-04a)
        for (const PropertyId dependency : entry->dependencies) {
            const BindingId producer = producer_of(dependency);
            // a dependency nothing writes is an input, so the walk stops there (§7.3)
            if (producer != 0 && producer != binding)
                visit(producer, order);
        }
        entry->visiting = false;
        // the binding appends after its sources, so a consumer never reads a stale value (§7.3, AD-04a)
        order.push_back(binding);
        // the ordered mark keeps a diamond from evaluating its shared tip twice in one pass (§7.3, AD-04a)
        entry->ordered = true;
    }
} // namespace albor
