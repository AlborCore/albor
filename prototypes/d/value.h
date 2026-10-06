#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace albor::prototype_d
{

    // cover the value categories the real property system must represent (§13.1)
    enum class ValueType : std::uint8_t
    {
        None,
        Bool,
        Int,
        Double,
        String,
        Snapshot
    };

    // hold application data that reaches bindings as shared immutable storage (§10.5)
    class SnapshotBox
    {
    public:
        SnapshotBox() = default;
        SnapshotBox(ValueType element_type, std::shared_ptr<const void> data, std::size_t count);

        [[nodiscard]] ValueType element_type() const { return m_element_type; }
        [[nodiscard]] const void* data() const { return m_data.get(); }
        [[nodiscard]] std::size_t count() const { return m_count; }

        // compare snapshots by storage identity because the payload is immutable (§10.5)
        [[nodiscard]] friend bool operator==(const SnapshotBox& left, const SnapshotBox& right) { return left.m_data == right.m_data && left.m_count == right.m_count && left.m_element_type == right.m_element_type; }

    private:
        std::shared_ptr<const void> m_data;
        std::size_t m_count = 0;
        ValueType m_element_type = ValueType::None;
    };

    // store the same fixed size variant in every slot so the strategies stay comparable (§13.3)
    class Value
    {
    public:
        Value() = default;
        Value(bool value); // NOLINT(google-explicit-constructor)
        Value(std::int64_t value); // NOLINT(google-explicit-constructor)
        Value(double value); // NOLINT(google-explicit-constructor)
        Value(std::shared_ptr<const std::string> value); // NOLINT(google-explicit-constructor)
        Value(SnapshotBox value); // NOLINT(google-explicit-constructor)

        static Value make_string(std::string text);

        [[nodiscard]] ValueType type() const;
        [[nodiscard]] bool as_bool() const;
        [[nodiscard]] std::int64_t as_int() const;
        [[nodiscard]] double as_double() const;
        [[nodiscard]] std::shared_ptr<const std::string> as_string() const;
        [[nodiscard]] SnapshotBox as_snapshot() const;

        // compare values so that an equal assignment never invalidates dependents (§13.3)
        [[nodiscard]] bool same_as(const Value& other) const;

        // count only the heap bytes whose size is known exactly
        [[nodiscard]] std::size_t known_heap_bytes() const;

        static constexpr std::size_t slot_bytes() { return sizeof(Storage); }

    private:
        using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::shared_ptr<const std::string>, SnapshotBox>;

        Storage m_storage;
    };

} // namespace albor::prototype_d
