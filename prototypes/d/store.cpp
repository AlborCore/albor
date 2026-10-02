#include "prototypes/d/store.h"

#include <utility>

namespace albor::prototype_d
{

    PropertyId PropertyStore::add(Value initial) {
        // prepare an empty slot for the new property
        PropertySlot new_slot;

        // seed the slot with generated initialization (§13.4)
        new_slot.value = std::move(initial);

        // append the slot to the dense slot array
        m_slots.push_back(std::move(new_slot));

        // hand back the dense index that identifies the property
        return static_cast<PropertyId>(m_slots.size() - 1);
    }

    const PropertySlot& PropertyStore::slot(PropertyId id) const { return m_slots.at(id); }

    Value PropertyStore::read(PropertyId id, Value fallback) const {
        // read the stored value for comparison against the requested type
        const Value& stored = m_slots.at(id).value;

        // return the fallback on a type mismatch to keep the read total
        return stored.type() == fallback.type() ? stored : fallback;
    }

    bool PropertyStore::assign(PropertyId id, Value value) {
        // an explicit write wins over an active animation (§13.5)
        drop_animation(id);

        // take ownership of the property and break its binding (§13.4)
        return store(id, std::move(value), WriteSource::Imperative);
    }

    void PropertyStore::post_set(PropertyId id, Value value) {
        // enqueue on the worker thread and apply on the UI thread in drain_inbox (§13.2)
        const std::lock_guard<std::mutex> lock(m_inbox_mutex);

        // store the pending write for the UI thread to apply
        m_inbox.push_back(PendingWrite{id, std::move(value)});
    }

    void PropertyStore::drain_inbox(std::vector<PropertyId>& changed_out) {
        // collect the queued writes outside the lock
        std::deque<PendingWrite> queued;

        {
            // move the queue out under the lock to apply the values without holding it
            const std::lock_guard<std::mutex> lock(m_inbox_mutex);

            // take ownership of the pending writes
            queued.swap(m_inbox);
        }

        // apply every write that the workers queued
        for (const PendingWrite& pending : queued) {
            // treat a queued worker write as imperative and break the binding (§13.4)
            const bool changed = store(pending.property, pending.value, WriteSource::Imperative);

            // report the property so that the engine can schedule a pass
            if (changed) {
                changed_out.push_back(pending.property);
            }
        }
    }

    std::size_t PropertyStore::pending_write_count() const {
        // read the queue size under the lock
        const std::lock_guard<std::mutex> lock(m_inbox_mutex);

        // report how many worker writes are still queued
        return m_inbox.size();
    }

    bool PropertyStore::write_from_binding(PropertyId id, Value value) {
        // write through the internal path that never breaks the binding (§13.4)
        return store(id, std::move(value), WriteSource::Binding);
    }

    bool PropertyStore::write_from_animation(PropertyId id, Value value) {
        // shadow the binding value once so that the animation can be undone (§13.5)
        if (source_of(id) != WriteSource::Animation) {
            m_shadowed.emplace(id, m_slots.at(id).value);
        }

        // publish the animated value without breaking the binding (§13.5)
        return store(id, std::move(value), WriteSource::Animation);
    }

    void PropertyStore::end_animation(PropertyId id) {
        // look up the binding value the animation shadowed
        const auto shadow = m_shadowed.find(id);

        // do nothing when no animation was running on the property
        if (shadow == m_shadowed.end()) {
            return;
        }

        // copy the shadowed value before the entry is dropped
        const Value restored = shadow->second;

        // forget the shadowed value now that it is either restored or dropped
        m_shadowed.erase(shadow);

        // restore the binding value that the animation shadowed (§13.5)
        store(id, restored, WriteSource::Binding);
    }

    bool PropertyStore::animated(PropertyId id) const { return source_of(id) == WriteSource::Animation; }

    std::size_t PropertyStore::animated_count() const { return m_shadowed.size(); }

    std::size_t PropertyStore::animation_side_bytes() const {
        // charge every shadowed value its payload and one entry of the side table
        std::size_t total = m_shadowed.size() * (sizeof(std::pair<const PropertyId, Value>) + sizeof(Value*) * 2);

        // add the payload bytes of every shadowed value
        for (const auto& [property, value] : m_shadowed) {
            // the property id is only read to keep the loop explicit
            static_cast<void>(property);

            total += value.known_heap_bytes();
        }

        // report the side table bytes
        return total;
    }

    WriteSource PropertyStore::source_of(PropertyId id) const { return m_slots.at(id).source; }

    std::uint64_t PropertyStore::generation_of(PropertyId id) const { return m_slots.at(id).generation; }

    void PropertyStore::attach_binding(PropertyId id) {
        // mark the property as owned by its binding
        m_slots.at(id).source = WriteSource::Binding;
    }

    bool PropertyStore::has_binding(PropertyId id) const { return m_slots.at(id).source == WriteSource::Binding; }

    void PropertyStore::rebind(PropertyId id) {
        // an explicit rebind also ends the animation (§13.5)
        drop_animation(id);

        // hand the property back to its declarative binding (§13.4)
        m_slots.at(id).source = WriteSource::Binding;
    }

    std::size_t PropertyStore::slot_bytes() const {
        // report the slot array without the value payloads it points at
        return m_slots.size() * sizeof(PropertySlot);
    }

    std::size_t PropertyStore::known_heap_bytes() const {
        // accumulate the payload bytes of every stored value
        std::size_t total = 0;

        // walk the slots of the component store
        for (const PropertySlot& current : m_slots) {
            // add the payload size the stored value knows exactly
            total += current.value.known_heap_bytes();
        }

        // report the accumulated payload bytes
        return total;
    }

    void PropertyStore::drop_animation(PropertyId id) {
        // do nothing when the animation layer does not own the property
        if (source_of(id) != WriteSource::Animation) {
            return;
        }

        // forget the shadowed binding value because the explicit write wins (§13.5)
        m_shadowed.erase(id);
    }

    bool PropertyStore::store(PropertyId id, Value value, WriteSource source) {
        // resolve the slot that receives the write
        PropertySlot& target = m_slots.at(id);

        // keep the generation when an assignment leaves the value unchanged (§13.3)
        if (target.value.same_as(value)) {
            // report that nothing changed so that no dependent is invalidated
            return false;
        }

        // publish the new value
        target.value = std::move(value);

        // record the write source that decides precedence (§13.4)
        target.source = source;

        // advance the change counter used for change detection
        target.generation++;

        // report the change that justifies invalidating dependents
        return true;
    }

} // namespace albor::prototype_d
