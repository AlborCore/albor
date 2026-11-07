#include "albor/runtime/binding_engine.h"

namespace albor
{
    namespace
    {
        // the low half of a handle is the slot number, so a dead slot can carry a new generation (§7.3)
        constexpr std::uint32_t BINDING_SLOT_MASK = 0xffffffffu;

        // the high half of a handle is the slot generation, which keeps a stale handle stale after reuse (§7.3)
        constexpr std::uint32_t BINDING_GENERATION_SHIFT = 32;

        // pack one slot and its generation into the handle the caller holds (§7.3)
        constexpr BindingId make_binding_id(std::uint32_t slot, std::uint32_t generation) { return (static_cast<BindingId>(generation) << BINDING_GENERATION_SHIFT) | static_cast<BindingId>(slot); }

        // unpack the slot number from a handle, where zero names no binding (§7.3)
        constexpr std::uint32_t binding_slot(BindingId binding) { return static_cast<std::uint32_t>(binding & BINDING_SLOT_MASK); }

        // unpack the generation from a handle, which a stale slot replacement no longer matches (§7.3)
        constexpr std::uint32_t binding_generation(BindingId binding) { return static_cast<std::uint32_t>(binding >> BINDING_GENERATION_SHIFT); }
    } // namespace

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

    BindingId BindingEngine::claim_slot(PropertyId target) {
        // a dead slot is reused so a rebuilt sink does not grow the slot vector (§7.3)
        if (!m_free_slots.empty()) {
            const std::uint32_t slot = m_free_slots.back();
            m_free_slots.pop_back();
            Binding& entry = m_bindings[slot - 1];
            // the generation advances, so a handle from the old binding can never name the replacement (§7.3)
            entry.generation++;
            // a wrap after 2^32 reuses restarts at one, a bound far beyond a session's rebuild count (§7.3)
            if (entry.generation == 0)
                entry.generation = 1;
            entry.target = target;
            entry.dependencies.clear();
            entry.evaluator = nullptr;
            entry.static_evaluator = nullptr;
            entry.static_context = nullptr;
            entry.dirty = true;
            entry.visiting = false;
            entry.ordered = false;
            return make_binding_id(slot, entry.generation);
        }
        // a fresh slot takes the next index, which is the only time the vector grows (§7.3)
        Binding binding;
        binding.target = target;
        binding.generation = 1;
        binding.dirty = true;
        m_bindings.push_back(std::move(binding));
        return make_binding_id(static_cast<std::uint32_t>(m_bindings.size()), m_bindings.back().generation);
    }

    BindingId BindingEngine::bind(PropertyId target, std::vector<PropertyId> dependencies, BindingEvaluator evaluator) {
        // a binding with no evaluator has no work, so it is refused rather than leaving a dead slot (§7.3)
        if (!evaluator)
            return 0;
        // a target may only carry one binding, because a second one would fight over the same property (§7.3)
        const std::uint64_t key = property_key(target);
        const auto existing = m_producers.find(key);
        if (existing != m_producers.end())
            unbind(existing->second);
        // the slot is reused when one is free, so a replacement does not grow the binding vector (§7.3)
        const BindingId handle = claim_slot(target);
        Binding& binding = *find(handle);
        binding.dependencies = std::move(dependencies);
        binding.evaluator = std::make_shared<BindingEvaluator>(std::move(evaluator));
        // every declared dependency gains this binding as a consumer, which is the reverse edge of the graph (§7.3)
        for (const PropertyId dependency : binding.dependencies)
            m_dependents[property_key(dependency)].push_back(handle);
        // the target records its writer, which is what orders a consumer after its source (§7.3, AD-04a)
        m_producers[key] = handle;
        // the target's consumers recompute after the replacement, so no consumer sees the old value (§7.3)
        notify_changed(target);
        return handle;
    }

    BindingId BindingEngine::bind_static(PropertyId target, std::span<const PropertyId> dependencies, StaticBindingFn evaluator, void* context) {
        // a static binding with no function has no work, so it is refused rather than leaving a dead slot (§7.3)
        if (evaluator == nullptr)
            return 0;
        // a target may only carry one binding, because a second one would fight over the same property (§7.3)
        const std::uint64_t key = property_key(target);
        const auto existing = m_producers.find(key);
        if (existing != m_producers.end())
            unbind(existing->second);
        // the slot is reused when one is free, so a replacement does not grow the binding vector (§7.3)
        const BindingId handle = claim_slot(target);
        Binding& binding = *find(handle);
        binding.dependencies.assign(dependencies.begin(), dependencies.end());
        binding.static_evaluator = evaluator;
        binding.static_context = context;
        // every declared dependency gains this binding as a consumer, which is the reverse edge of the graph (§7.3)
        for (const PropertyId dependency : binding.dependencies)
            m_dependents[property_key(dependency)].push_back(handle);
        // the target records its writer, which is what orders a consumer after its source (§7.3, AD-04a)
        m_producers[key] = handle;
        // the target's consumers recompute after the replacement, so no consumer sees the old value (§7.3)
        notify_changed(target);
        return handle;
    }

    bool BindingEngine::unbind(BindingId binding) {
        // a stale handle names nothing, so the caller learns the removal did not happen (§7.3)
        Binding* entry = find(binding);
        if (entry == nullptr || !entry->registered())
            return false;
        // every dependency loses the consumer edge, so a later change no longer reaches it (§7.3)
        for (const PropertyId dependency : entry->dependencies) {
            const std::uint64_t dependency_key = property_key(dependency);
            const auto found = m_dependents.find(dependency_key);
            // a dependency declared twice finds no edge the second time, so it is skipped (§7.3)
            if (found == m_dependents.end())
                continue;
            // every matching edge goes, so a dependency declared twice cannot leave a dead consumer behind (§7.3)
            std::erase(found->second, binding);
            // an empty reverse edge goes too, so a dead dependency does not hold a map entry (§7.3)
            if (found->second.empty())
                m_dependents.erase(found);
        }
        // the producer edge goes with it, so a consumer no longer waits for a dead writer (§7.3)
        const std::uint64_t key = property_key(entry->target);
        const auto producer = m_producers.find(key);
        if (producer != m_producers.end() && producer->second == binding)
            m_producers.erase(producer);
        // the handle names no target once unbound, so target() agrees with bound() (§7.3)
        entry->target = invalid_property_id();
        // the binding is emptied rather than erased, so no handle ever names a different binding (§7.3)
        entry->evaluator = nullptr;
        entry->static_evaluator = nullptr;
        entry->static_context = nullptr;
        entry->dependencies.clear();
        entry->dirty = false;
        entry->visiting = false;
        entry->ordered = false;
        // the dead slot is queued for reuse, which bounds the storage a rebuild cycle can take (§7.3)
        m_free_slots.push_back(binding_slot(binding));
        return true;
    }

    bool BindingEngine::bound(BindingId binding) const {
        // an unbound handle keeps its slot but carries no work (§7.3)
        const Binding* entry = find(binding);
        return entry != nullptr && entry->registered();
    }

    PropertyId BindingEngine::target(BindingId binding) const {
        // find the binding entry by its handle (§7.3)
        const Binding* entry = find(binding);
        // a stale or unbound handle names no target, which matches bound() (§7.3)
        if (entry == nullptr)
            return invalid_property_id();
        // the target property
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
            if (binding.registered())
                count++;
        return count;
    }

    std::size_t BindingEngine::binding_slot_count() const {
        // the slot count includes dead slots, because that is the storage a test measures (§7.3)
        return m_bindings.size();
    }

    void BindingEngine::notify_changed(PropertyId source) {
        const auto found = m_dependents.find(property_key(source));
        // a property nothing depends on settles nothing, which is the common case of a leaf (§7.3)
        if (found == m_dependents.end())
            return;
        // every registered consumer becomes dirty, which is the eager push of the change (§7.3, AD-04c)
        for (const BindingId binding : found->second)
            mark_dirty(binding);
    }

    void BindingEngine::invalidate(BindingId binding) {
        // an animation presence write marks its own binding without walking the graph (AD-04d)
        mark_dirty(binding);
    }

    void BindingEngine::mark_dirty(BindingId binding) {
        Binding* entry = find(binding);
        // a stale handle marks nothing (§7.3)
        if (entry == nullptr || !entry->registered())
            return;
        entry->dirty = true;
        // a binding outside the active order joins it now, so a write settles its consumers in the same pass (§7.3)
        if (m_settle_order != nullptr && !entry->ordered)
            visit(binding, *m_settle_order);
    }

    std::size_t BindingEngine::settle() {
        std::size_t passes = 0;
        std::vector<BindingId> order;
        // the active order is shared with writes, so a consumer a binding dirties joins the same pass (§7.3)
        m_settle_order = &order;
        // the engine repeats until the graph converges or the cycle budget is spent (§7.3, AD-04c)
        while (passes < EVALUATION_BUDGET) {
            order.clear();
            // the ordered marks clear each pass, so a producer still precedes its consumer in every pass (§7.3, AD-04a)
            for (Binding& binding : m_bindings)
                binding.ordered = false;
            // every dirty binding enters the walk, which orders sources before their consumers (§7.3, AD-04a)
            for (std::size_t index = 0; index < m_bindings.size(); index++)
                if (m_bindings[index].dirty)
                    visit(make_binding_id(static_cast<std::uint32_t>(index + 1), m_bindings[index].generation), order);
            // a converged graph leaves nothing dirty, so an empty walk ends the settle (§7.3)
            if (order.empty())
                break;
            for (std::size_t cursor = 0; cursor < order.size(); cursor++) {
                const BindingId binding = order[cursor];
                Binding* entry = find(binding);
                // an unbound binding that a previous pass removed is skipped (§7.3)
                if (entry == nullptr)
                    continue;
                // an unbound binding keeps its slot but has no work, so the walk clears its flag and skips it (§7.3)
                if (!entry->registered()) {
                    entry->dirty = false;
                    continue;
                }
                // the dirty flag clears before the run, so a binding that reads its own target still converges (§7.3)
                entry->dirty = false;
                // a static binding runs its plain function over the recorded context, the allocation free path (§13.3)
                if (entry->static_evaluator != nullptr) {
                    const StaticBindingFn evaluator = entry->static_evaluator;
                    void* const context = entry->static_context;
                    evaluator(context);
                }
                else {
                    // the shared callable stays alive, because the evaluator may register or rebind while it runs (§7.3)
                    const std::shared_ptr<BindingEvaluator> evaluator = entry->evaluator;
                    (*evaluator)();
                }
                m_evaluations++;
            }
            passes++;
        }
        // the order pointer belongs to this call, so a later write does not touch a dead vector (§7.3)
        m_settle_order = nullptr;
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
        // handle zero and a slot past the end both name no binding (§7.3)
        const std::uint32_t slot = binding_slot(binding);
        if (slot == 0 || slot > m_bindings.size())
            return nullptr;
        Binding& entry = m_bindings[slot - 1];
        // a generation mismatch means the slot was reused, so the old handle stays stale (§7.3)
        if (entry.generation != binding_generation(binding))
            return nullptr;
        return &entry;
    }

    const BindingEngine::Binding* BindingEngine::find(BindingId binding) const {
        // handle zero and a slot past the end both name no binding (§7.3)
        const std::uint32_t slot = binding_slot(binding);
        if (slot == 0 || slot > m_bindings.size())
            return nullptr;
        const Binding& entry = m_bindings[slot - 1];
        // a generation mismatch means the slot was reused, so the old handle stays stale (§7.3)
        if (entry.generation != binding_generation(binding))
            return nullptr;
        return &entry;
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
        // a binding that already took its place is not walked twice, keeping a diamond single pass (§7.3, AD-04a)
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
