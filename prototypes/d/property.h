#pragma once

#include <cstdint>
#include <string>

#include "prototypes/d/property_id.h"
#include "prototypes/d/store.h"
#include "prototypes/d/value.h"

namespace albor::prototype_d
{

    // convert between a stored value and the type a binding expects
    template <typename T>
    struct ValueTraits;

    template <>
    struct ValueTraits<bool>
    {
        static Value to_value(bool value) { return Value(value); }
        static bool from_value(const Value& value) { return value.as_bool(); }
        static Value fallback() { return Value(false); }
    };

    template <>
    struct ValueTraits<std::int64_t>
    {
        static Value to_value(std::int64_t value) { return Value(value); }
        static std::int64_t from_value(const Value& value) { return value.as_int(); }
        static Value fallback() { return Value(std::int64_t{0}); }
    };

    template <>
    struct ValueTraits<double>
    {
        static Value to_value(double value) { return Value(value); }
        static double from_value(const Value& value) { return value.as_double(); }
        static Value fallback() { return Value(0.0); }
    };

    template <>
    struct ValueTraits<std::string>
    {
        static Value to_value(const std::string& value) { return Value::make_string(value); }
        static std::string from_value(const Value& value) {
            const std::shared_ptr<const std::string> text = value.as_string();
            return text ? *text : std::string();
        }

        static Value fallback() { return Value::make_string(std::string()); }
    };

    // hold a stored property the way generated code holds it (§13.3)
    template <typename T>
    class Property
    {
    public:
        Property() = default;
        Property(PropertyStore& store, PropertyId id) :
            m_store(&store), m_id(id) {}

        [[nodiscard]] bool valid() const { return m_store != nullptr && m_id != INVALID_PROPERTY; }
        [[nodiscard]] PropertyId id() const { return m_id; }

        [[nodiscard]] T get() const { return ValueTraits<T>::from_value(m_store->read(m_id, ValueTraits<T>::fallback())); }

        // break the owning binding on an imperative assignment (§13.4)
        void set(T value) {
            const bool changed = m_store->assign(m_id, ValueTraits<T>::to_value(value));
            static_cast<void>(changed);
        }

        // queue the assignment from any thread for drain_inbox (§13.2)
        void post_set(T value) { m_store->post_set(m_id, ValueTraits<T>::to_value(value)); }

        // write through the initialization path that keeps the binding (§13.4)
        void initialize(T value) {
            const bool changed = m_store->write_from_binding(m_id, ValueTraits<T>::to_value(value));
            static_cast<void>(changed);
        }

        [[nodiscard]] bool bound() const { return m_store->has_binding(m_id); }
        void rebind() { m_store->rebind(m_id); }

    private:
        PropertyStore* m_store = nullptr;
        PropertyId m_id = INVALID_PROPERTY;
    };

} // namespace albor::prototype_d
