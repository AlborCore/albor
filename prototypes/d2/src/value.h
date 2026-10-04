#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace albor::prototype_d2
{

    // name every property value category the real property system must represent (§13.1)
    enum class ValueType : std::uint8_t
    {
        None,
        Bool,
        Int,
        Double,
        String,
        TranslatedString,
        Snapshot
    };

    // report the byte size one value of a type occupies inline, without any discriminant (§13.3)
    [[nodiscard]] std::size_t inline_size_of(ValueType type);

    // report the alignment one value of a type requires (§13.3)
    [[nodiscard]] std::size_t inline_align_of(ValueType type);

    // report whether a type owns a heap payload that must be constructed and destroyed (§13.1)
    [[nodiscard]] bool is_non_trivial(ValueType type);

    // hold the sample array a snapshot payload points at, so data() hands out the samples and not the box (§10.5)
    struct SamplePayload
    {
        std::vector<double> samples;
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

        // report the payload bytes so the memory measurement can separate slot from payload (§28.2)
        [[nodiscard]] std::size_t payload_bytes() const;

        // hand out the samples themselves, because data() points at the payload box and not at the array (§10.5)
        [[nodiscard]] const double* samples() const { return static_cast<const SamplePayload*>(m_data.get())->samples.data(); }

        // compare snapshots by storage identity because the payload is immutable (§10.5)
        [[nodiscard]] bool same_as(const SnapshotBox& other) const { return m_data == other.m_data && m_count == other.m_count && m_element_type == other.m_element_type; }

    private:
        std::shared_ptr<const void> m_data;
        std::size_t m_count = 0;
        ValueType m_element_type = ValueType::None;
    };

    // hold a locale-dependent string, which is never folded as a constant (§13.1 category 9)
    class TranslatedString
    {
    public:
        TranslatedString() = default;
        TranslatedString(std::uint32_t locale_id, std::shared_ptr<const std::string> text);

        [[nodiscard]] std::uint32_t locale_id() const { return m_locale_id; }
        [[nodiscard]] const std::string* text() const { return m_text.get(); }
        [[nodiscard]] std::size_t payload_bytes() const;

        // compare the locale and the payload identity, since a translated string depends on both (§13.1)
        [[nodiscard]] bool same_as(const TranslatedString& other) const { return m_locale_id == other.m_locale_id && m_text == other.m_text; }

    private:
        std::uint32_t m_locale_id = 0;
        std::shared_ptr<const std::string> m_text;
    };

    // hold one property value in a fixed size variant so the erased candidate stays comparable (§13.3)
    class Value
    {
    public:
        Value() = default;
        Value(bool value); // NOLINT(google-explicit-constructor)
        Value(std::int64_t value); // NOLINT(google-explicit-constructor)
        Value(double value); // NOLINT(google-explicit-constructor)
        Value(std::shared_ptr<const std::string> value); // NOLINT(google-explicit-constructor)
        Value(TranslatedString value); // NOLINT(google-explicit-constructor)
        Value(SnapshotBox value); // NOLINT(google-explicit-constructor)

        static Value make_string(std::string text);

        // build a translated string for a locale, standing in for the i18n catalogue lookup (§23.3)
        static Value make_translated(std::string text, std::uint32_t locale_id);

        // build a snapshot payload of samples, standing in for worker published data (§10.5)
        static Value make_snapshot(std::shared_ptr<const std::vector<double>> samples);

        [[nodiscard]] ValueType type() const;
        [[nodiscard]] bool as_bool() const;
        [[nodiscard]] std::int64_t as_int() const;
        [[nodiscard]] double as_double() const;
        [[nodiscard]] std::shared_ptr<const std::string> as_string() const;
        [[nodiscard]] TranslatedString as_translated() const;
        [[nodiscard]] SnapshotBox as_snapshot() const;

        // compare values so an equal write never invalidates a dependent (§13.3)
        [[nodiscard]] bool same_as(const Value& other) const;

        // count only the heap bytes whose size is known exactly (§28.2)
        [[nodiscard]] std::size_t known_heap_bytes() const;

        static constexpr std::size_t slot_bytes() { return sizeof(Storage); }

    private:
        using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::shared_ptr<const std::string>, TranslatedString, SnapshotBox>;

        Storage m_storage;
    };

} // namespace albor::prototype_d2