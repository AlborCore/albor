#include "prototypes/d2/src/value.h"

#include <algorithm>

namespace albor::prototype_d2
{

    namespace
    {

        // read one double out of an opaque snapshot payload for the change comparison (§10.5)
        double sample_at(const void* data, std::size_t index) {
            // a null payload has no samples to read
            if (data == nullptr) {
                return 0.0;
            }

            // reinterpret the immutable payload as the sample array it was built from
            return static_cast<const double*>(data)[index];
        }

        // count the bytes of a shared string payload without its header (§28.2)
        std::size_t string_payload_bytes(const std::shared_ptr<const std::string>& text) { return text ? text->size() : 0; }

    } // namespace

    std::size_t inline_size_of(ValueType type) {
        // report the inline size that typed storage gives each value category (§13.3)
        switch (type) {
        case ValueType::None:
            return 1;
        case ValueType::Bool:
            return sizeof(bool);
        case ValueType::Int:
            return sizeof(std::int64_t);
        case ValueType::Double:
            return sizeof(double);
        case ValueType::String:
            return sizeof(std::shared_ptr<const std::string>);
        case ValueType::TranslatedString:
            return sizeof(TranslatedString);
        case ValueType::Snapshot:
            return sizeof(SnapshotBox);
        }

        // report one byte for an unknown type so a schema error cannot read out of bounds
        return 1;
    }

    std::size_t inline_align_of(ValueType type) {
        // report the alignment that inline storage must respect (§13.3)
        switch (type) {
        case ValueType::None:
            return 1;
        case ValueType::Bool:
            return alignof(bool);
        case ValueType::Int:
            return alignof(std::int64_t);
        case ValueType::Double:
            return alignof(double);
        case ValueType::String:
            return alignof(std::shared_ptr<const std::string>);
        case ValueType::TranslatedString:
            return alignof(TranslatedString);
        case ValueType::Snapshot:
            return alignof(SnapshotBox);
        }

        // fall back to byte alignment for an unknown type
        return 1;
    }

    bool is_non_trivial(ValueType type) {
        // a payload owning type needs construction and destruction, a scalar does not (§13.1)
        return type == ValueType::String || type == ValueType::TranslatedString || type == ValueType::Snapshot;
    }

    SnapshotBox::SnapshotBox(ValueType element_type, std::shared_ptr<const void> data, std::size_t count) :
        m_data(std::move(data)), m_count(count), m_element_type(element_type) {}

    std::size_t SnapshotBox::payload_bytes() const {
        // report the double samples the payload holds (§10.5)
        return m_count * sizeof(double);
    }

    TranslatedString::TranslatedString(std::uint32_t locale_id, std::shared_ptr<const std::string> text) :
        m_locale_id(locale_id), m_text(std::move(text)) {}

    std::size_t TranslatedString::payload_bytes() const { return string_payload_bytes(m_text); }

    std::string_view policy_name(EqualityPolicy policy) {
        // name the policy so a measurement row is unambiguous (§28.2)
        switch (policy) {
        case EqualityPolicy::Exact:
            return "exact";
        case EqualityPolicy::Ieee:
            return "ieee";
        case EqualityPolicy::BitPattern:
            return "bit_pattern";
        case EqualityPolicy::Tolerant:
            return "tolerant";
        case EqualityPolicy::StringContent:
            return "string_content";
        }

        // fall back to a name that cannot be mistaken for a known policy
        return "unknown";
    }

    Value::Value(bool value) :
        m_storage(value) {}

    Value::Value(std::int64_t value) :
        m_storage(value) {}

    Value::Value(double value) :
        m_storage(value) {}

    Value::Value(std::shared_ptr<const std::string> value) :
        m_storage(std::move(value)) {}

    Value::Value(TranslatedString value) :
        m_storage(std::move(value)) {}

    Value::Value(SnapshotBox value) :
        m_storage(std::move(value)) {}

    Value Value::make_string(std::string text) {
        // share one immutable string so the payload is not copied per property (§10.5)
        return Value(std::make_shared<const std::string>(std::move(text)));
    }

    Value Value::make_translated(std::string text, std::uint32_t locale_id) {
        // keep the locale beside the payload so a locale change re-evaluates the binding (§13.1)
        return Value(TranslatedString(locale_id, std::make_shared<const std::string>(std::move(text))));
    }

    Value Value::make_snapshot(std::shared_ptr<const std::vector<double>> samples) {
        // wrap the samples in their own box, so the stored pointer is the payload and not the vector header (§10.5)
        auto payload = std::make_shared<SamplePayload>();

        // copy the samples into the payload, which is then shared immutably (§10.5)
        if (samples) {
            payload->samples = *samples;
        }

        // hand the payload over as immutable storage (§10.5)
        std::shared_ptr<const SamplePayload> immutable = payload;

        // count the samples so the payload can be charged to the property (§10.5)
        const std::size_t count = payload->samples.size();

        // hand the immutable payload to the snapshot box (§10.5)
        return Value(SnapshotBox(ValueType::Double, std::move(immutable), count));
    }

    ValueType Value::type() const {
        // read the discriminant the variant carries (§13.3)
        return static_cast<ValueType>(m_storage.index());
    }

    bool Value::as_bool() const { return std::holds_alternative<bool>(m_storage) ? std::get<bool>(m_storage) : false; }

    std::int64_t Value::as_int() const { return std::holds_alternative<std::int64_t>(m_storage) ? std::get<std::int64_t>(m_storage) : 0; }

    double Value::as_double() const { return std::holds_alternative<double>(m_storage) ? std::get<double>(m_storage) : 0.0; }

    std::shared_ptr<const std::string> Value::as_string() const { return std::holds_alternative<std::shared_ptr<const std::string>>(m_storage) ? std::get<std::shared_ptr<const std::string>>(m_storage) : nullptr; }

    TranslatedString Value::as_translated() const { return std::holds_alternative<TranslatedString>(m_storage) ? std::get<TranslatedString>(m_storage) : TranslatedString(); }

    SnapshotBox Value::as_snapshot() const { return std::holds_alternative<SnapshotBox>(m_storage) ? std::get<SnapshotBox>(m_storage) : SnapshotBox(); }

    bool Value::same_as(const Value& other) const {
        // compare the discriminant first so a mismatched pair never reads the wrong alternative
        if (type() != other.type()) {
            return false;
        }

        // compare the alternative that the discriminant selected
        switch (type()) {
        case ValueType::None:
            return true;
        case ValueType::Bool:
            return as_bool() == other.as_bool();
        case ValueType::Int:
            return as_int() == other.as_int();
        case ValueType::Double:
            return as_double() == other.as_double();
        case ValueType::String:
            return as_string() == other.as_string();
        case ValueType::TranslatedString:
            return as_translated().same_as(other.as_translated());
        case ValueType::Snapshot: {
            // compare snapshots by payload identity, then by content for a freshly built equal payload
            const SnapshotBox left = as_snapshot();
            const SnapshotBox right = other.as_snapshot();

            // an identical payload pointer is the common case and needs no content read
            if (left.same_as(right)) {
                return true;
            }

            // fall through to a content comparison when the pointers differ (§10.5)
            const double* left_samples = left.data() == nullptr ? nullptr : left.samples();
            const double* right_samples = right.data() == nullptr ? nullptr : right.samples();

            // a different sample count means the payloads differ
            if (left.count() != right.count()) {
                return false;
            }

            // compare every sample so an equal publish does not invalidate readers (§13.2)
            for (std::size_t index = 0; index < left.count(); index++) {
                if (sample_at(left_samples, index) != sample_at(right_samples, index)) {
                    return false;
                }
            }

            // report the equal payload
            return true;
        }
        }

        // report an unknown discriminant as changed so it is never silently treated as equal
        return false;
    }

    bool Value::equal_under(const Value& other, const EqualityPolicy policy) const {
        // compare the discriminant first so a mismatched pair never reads the wrong alternative
        if (type() != other.type()) {
            return false;
        }

        // compare the alternative that the discriminant selected, under the chosen rule (§13.3)
        switch (type()) {
        case ValueType::None:
            return true;
        case ValueType::Bool:
            return as_bool() == other.as_bool();
        case ValueType::Int:
            return as_int() == other.as_int();
        case ValueType::Double: {
            // read both doubles once, because every double policy reads them (§13.1)
            const double left = as_double();
            const double right = other.as_double();

            // the exact and ieee policies are the same comparison under two names, and are measured separately so the
            // report can show that the naming is not the difference (plan.md R6)
            if (policy == EqualityPolicy::Exact || policy == EqualityPolicy::Ieee || policy == EqualityPolicy::StringContent) {
                return left == right;
            }

            // the bit-pattern policy compares the representation, so a NaN equals itself and -0.0 differs from 0.0 (§13.1)
            if (policy == EqualityPolicy::BitPattern) {
                std::uint64_t left_bits = 0;
                std::uint64_t right_bits = 0;
                static_assert(sizeof(left_bits) == sizeof(left));
                __builtin_memcpy(&left_bits, &left, sizeof(left_bits));
                __builtin_memcpy(&right_bits, &right, sizeof(right_bits));
                return left_bits == right_bits;
            }

            // the tolerant policy suppresses an invalidation inside a relative epsilon, which is what a developer
            // reading a rounded value expects not to happen (§13.3)
            const double difference = std::abs(left - right);
            const double scale = std::max(1.0, std::max(std::abs(left), std::abs(right)));
            return difference <= 1e-9 * scale;
        }
        case ValueType::String: {
            // the exact policies compare the payload pointer, so an equal string behind a fresh allocation reads as a change (§13.1)
            if (policy != EqualityPolicy::StringContent) {
                return as_string() == other.as_string();
            }

            // the content policy compares the characters, which is what detects a change behind a fresh payload (§13.1)
            const std::shared_ptr<const std::string> left = as_string();
            const std::shared_ptr<const std::string> right = other.as_string();

            // a payload both sides hold is the common case and needs no character read (§28.2)
            if (left == right) {
                return true;
            }

            // a missing payload equals another missing payload, and differs from a present one (§13.1)
            if (!left || !right) {
                return false;
            }

            // compare the characters (§13.1)
            return *left == *right;
        }
        case ValueType::TranslatedString: {
            // a locale change is always a change, because the value depends on the locale (§13.1)
            const TranslatedString left = as_translated();
            const TranslatedString right = other.as_translated();

            if (left.locale_id() != right.locale_id()) {
                return false;
            }

            // the exact policies compare the payload pointer (§13.1)
            if (policy != EqualityPolicy::StringContent) {
                return left.same_as(right);
            }

            // a payload both sides hold needs no character read (§28.2)
            if (left.text() == right.text()) {
                return true;
            }

            // a missing payload equals another missing payload, and differs from a present one (§13.1)
            if (!left.text() || !right.text()) {
                return false;
            }

            // compare the characters of both locales' text (§13.1)
            return *left.text() == *right.text();
        }
        case ValueType::Snapshot: {
            const SnapshotBox left = as_snapshot();
            const SnapshotBox right = other.as_snapshot();

            // an identical payload pointer is the common case and needs no content read (§10.5)
            if (left.same_as(right)) {
                return true;
            }

            // the bit-pattern policy stops at identity, so a fresh equal payload reads as a change (§10.5)
            if (policy == EqualityPolicy::BitPattern) {
                return false;
            }

            // a different sample count means the payloads differ (§10.5)
            if (left.count() != right.count()) {
                return false;
            }

            // an element type the samples cannot be read as means the payloads differ (§10.5)
            if (left.element_type() != right.element_type()) {
                return false;
            }

            // compare every sample so an equal publish does not invalidate readers (§13.2)
            const double* left_samples = left.data() == nullptr ? nullptr : left.samples();
            const double* right_samples = right.data() == nullptr ? nullptr : right.samples();

            for (std::size_t index = 0; index < left.count(); index++) {
                if (sample_at(left_samples, index) != sample_at(right_samples, index)) {
                    return false;
                }
            }

            // report the equal payload
            return true;
        }
        }

        // report an unknown discriminant as changed so it is never silently treated as equal
        return false;
    }

    std::size_t Value::known_heap_bytes() const {
        // charge only the payload of the alternative the value actually holds (§28.2)
        switch (type()) {
        case ValueType::String:
            return string_payload_bytes(as_string());
        case ValueType::TranslatedString:
            return as_translated().payload_bytes();
        case ValueType::Snapshot:
            return as_snapshot().payload_bytes();
        case ValueType::None:
        case ValueType::Bool:
        case ValueType::Int:
        case ValueType::Double:
            break;
        }

        // a scalar owns no payload
        return 0;
    }

} // namespace albor::prototype_d2
