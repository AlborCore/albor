#pragma once

#include <cstdint>
#include <span>

#include "prototypes/d/property_id.h"
#include "prototypes/d/store.h"
#include "prototypes/d/value.h"

namespace albor::prototype_d
{

    // notify the engine so that only a real change schedules another pass (§13.3)
    using WriteObserver = void (*)(void* user, PropertyId property, bool changed);

    // pass the binding a plain context value with no heap allocation (§13.3)
    class BindingContext
    {
    public:
        BindingContext(PropertyStore& store, PropertyId target, BindingId self, std::span<const PropertyId> dependencies, WriteObserver observer, void* user) :
            m_store(store), m_target(target), m_self(self), m_dependencies(dependencies), m_observer(observer), m_user(user) {}

        [[nodiscard]] const Value& read(PropertyId id) const { return m_store.slot(id).value; }

        // expose the running binding id and its declared dependencies (§13.3)
        [[nodiscard]] BindingId self() const { return m_self; }
        [[nodiscard]] std::span<const PropertyId> declared() const { return m_dependencies; }
        [[nodiscard]] std::int64_t read_int(PropertyId id) const { return m_store.read(id, Value(std::int64_t{0})).as_int(); }
        [[nodiscard]] double read_double(PropertyId id) const { return m_store.read(id, Value(0.0)).as_double(); }
        [[nodiscard]] bool read_bool(PropertyId id) const { return m_store.read(id, Value(false)).as_bool(); }

        // write only the binding target through the path that keeps the binding (§13.4)
        void write(Value value) const {
            // drop the write while the animation layer owns the target (§13.5)
            if (m_store.animated(m_target)) {
                return;
            }

            const bool changed = m_store.write_from_binding(m_target, std::move(value));

            if (m_observer != nullptr) {
                m_observer(m_user, m_target, changed);
            }
        }

        void write_int(std::int64_t value) const { write(Value(value)); }
        void write_double(double value) const { write(Value(value)); }
        void write_bool(bool value) const { write(Value(value)); }

    private:
        PropertyStore& m_store;
        PropertyId m_target;
        BindingId m_self;
        std::span<const PropertyId> m_dependencies;
        WriteObserver m_observer;
        void* m_user;
    };

    // compile a binding into a plain function plus declared property ids (§13.3)
    using BindingFn = void (*)(BindingContext&);

} // namespace albor::prototype_d
