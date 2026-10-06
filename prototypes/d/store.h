#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "prototypes/d/property_id.h"
#include "prototypes/d/value.h"

namespace albor::prototype_d
{

    // choose the write source that decides precedence between initialization, binding, animation and assignment (§13.4, §13.5)
    enum class WriteSource : std::uint8_t
    {
        GeneratedInit,
        Binding,
        Animation,
        Imperative
    };

    // hold the value, its write source and the generation counter used for change detection
    struct PropertySlot
    {
        Value value;
        WriteSource source = WriteSource::GeneratedInit;
        std::uint64_t generation = 0;
    };

    // queue a worker assignment until the UI thread applies it (§13.2)
    struct PendingWrite
    {
        PropertyId property = INVALID_PROPERTY;
        Value value;
    };

    // store dense property slots, detect changes and buffer thread safe writes
    class PropertyStore
    {
    public:
        PropertyStore() = default;

        PropertyStore(const PropertyStore&) = delete;
        PropertyStore& operator=(const PropertyStore&) = delete;
        PropertyStore(PropertyStore&&) = delete;
        PropertyStore& operator=(PropertyStore&&) = delete;

        // allocate a slot with generated initialization that never breaks a binding (§13.4)
        [[nodiscard]] PropertyId add(Value initial);

        [[nodiscard]] std::size_t size() const { return m_slots.size(); }

        [[nodiscard]] const PropertySlot& slot(PropertyId id) const;

        // read a property and substitute the fallback on a type mismatch
        [[nodiscard]] Value read(PropertyId id, Value fallback) const;

        // break the owning binding on an imperative assignment from the UI thread (§13.4)
        [[nodiscard]] bool assign(PropertyId id, Value value);

        // queue an assignment from any thread for the UI thread (§13.2)
        void post_set(PropertyId id, Value value);

        // apply the queued worker writes and collect the properties that changed
        void drain_inbox(std::vector<PropertyId>& changed_out);

        [[nodiscard]] std::size_t pending_write_count() const;

        // write through the internal path that never breaks the binding (§13.4)
        [[nodiscard]] bool write_from_binding(PropertyId id, Value value);

        // write from the animation layer, which shadows the binding value and never breaks the binding (§13.5)
        [[nodiscard]] bool write_from_animation(PropertyId id, Value value);

        // end the animation and restore the shadowed binding value (§13.5)
        void end_animation(PropertyId id);

        // report whether the animation layer currently owns the property
        [[nodiscard]] bool animated(PropertyId id) const;

        // report how many properties the animation layer currently owns
        [[nodiscard]] std::size_t animated_count() const;

        // report the side table that holds the shadowed binding values (§28.2)
        [[nodiscard]] std::size_t animation_side_bytes() const;

        [[nodiscard]] WriteSource source_of(PropertyId id) const;

        [[nodiscard]] std::uint64_t generation_of(PropertyId id) const;

        // attach a binding so that the property follows the binding result (§13.4)
        void attach_binding(PropertyId id);

        [[nodiscard]] bool has_binding(PropertyId id) const;

        // restore the binding that an imperative assignment broke (§13.4)
        void rebind(PropertyId id);

        [[nodiscard]] std::size_t slot_bytes() const;

        [[nodiscard]] std::size_t known_heap_bytes() const;

    private:
        // write a slot and report the change that justifies invalidating dependents
        bool store(PropertyId id, Value value, WriteSource source);

        // take the animation layer away from a property when an explicit write wins (§13.5)
        void drop_animation(PropertyId id);

        std::vector<PropertySlot> m_slots;
        std::deque<PendingWrite> m_inbox;

        // hold the binding values that an active animation shadows (§13.5)
        std::unordered_map<PropertyId, Value> m_shadowed;
        mutable std::mutex m_inbox_mutex;
    };

} // namespace albor::prototype_d
