#include "prototypes/d/value.h"

#include <utility>

namespace albor::prototype_d
{

    SnapshotBox::SnapshotBox(ValueType element_type, std::shared_ptr<const void> data, std::size_t count) :
        m_data(std::move(data)), m_count(count), m_element_type(element_type) {}

    Value::Value(bool value) :
        m_storage(value) {}

    Value::Value(std::int64_t value) :
        m_storage(value) {}

    Value::Value(double value) :
        m_storage(value) {}

    Value::Value(std::shared_ptr<const std::string> value) :
        m_storage(std::move(value)) {}

    Value::Value(SnapshotBox value) :
        m_storage(std::move(value)) {}

    Value Value::make_string(std::string text) {
        // share the string as immutable const storage so that copying a value never copies the text
        return Value(std::shared_ptr<const std::string>(std::make_shared<const std::string>(std::move(text))));
    }

    ValueType Value::type() const {
        // map the held alternative to its value type
        return std::visit(
            [](const auto& held) -> ValueType {
                // name the alternative so that the type can be compared at compile time
                using Held = std::decay_t<decltype(held)>;

                // report the empty value type
                if constexpr (std::is_same_v<Held, std::monostate>) {
                    return ValueType::None;
                }
                else if constexpr (std::is_same_v<Held, bool>) {
                    // report the boolean value type
                    return ValueType::Bool;
                }
                else if constexpr (std::is_same_v<Held, std::int64_t>) {
                    // report the integer value type
                    return ValueType::Int;
                }
                else if constexpr (std::is_same_v<Held, double>) {
                    // report the floating point value type
                    return ValueType::Double;
                }
                else if constexpr (std::is_same_v<Held, std::shared_ptr<const std::string>>) {
                    // report the shared string value type
                    return ValueType::String;
                }
                else {
                    // report the snapshot value type
                    return ValueType::Snapshot;
                }
            },
            m_storage);
    }

    bool Value::as_bool() const { return std::get<bool>(m_storage); }

    std::int64_t Value::as_int() const { return std::get<std::int64_t>(m_storage); }

    double Value::as_double() const { return std::get<double>(m_storage); }

    std::shared_ptr<const std::string> Value::as_string() const { return std::get<std::shared_ptr<const std::string>>(m_storage); }

    SnapshotBox Value::as_snapshot() const { return std::get<SnapshotBox>(m_storage); }

    bool Value::same_as(const Value& other) const { return m_storage == other.m_storage; }

    std::size_t Value::known_heap_bytes() const {
        // charge only the alternatives that own a payload of known size
        return std::visit(
            [](const auto& held) -> std::size_t {
                // name the alternative so that the payload can be inspected at compile time
                using Held = std::decay_t<decltype(held)>;

                // charge only the characters because the control block size is allocator defined
                if constexpr (std::is_same_v<Held, std::shared_ptr<const std::string>>) {
                    return held ? held->size() : 0;
                }
                else {
                    // charge nothing for the inline alternatives
                    return 0;
                }
            },
            m_storage);
    }

} // namespace albor::prototype_d
