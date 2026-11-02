#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace albor::model
{
    // the stable identity of one item, which a row is keyed by and which is a value not an address (§18.1, AD-11, AD-13)
    class ItemKey
    {
    public:
        // build the empty key, which no item carries, because a row always has an identity (§18.1, AD-11)
        ItemKey() = default;

        // build a key from text, which is what a text field contributes (§18.1, AD-13)
        ItemKey(std::string text) :
            m_text(std::move(text)) {}

        // build a key from a whole number, which is what an integer field contributes (§18.1, AD-13)
        [[nodiscard]] static ItemKey from_int(long long value);

        // hand out the text, which is the identity as written (§18.1, AD-13)
        [[nodiscard]] const std::string& text() const { return m_text; }

        // report whether two keys name the same item, which is what a row match asks (§18.1, AD-13)
        [[nodiscard]] bool operator==(const ItemKey& other) const { return m_text == other.m_text; }

    private:
        // the identity as text, which keeps the key a value rather than an address (§18.1, AD-13)
        std::string m_text;
    };
} // namespace albor::model

namespace std
{
    // the hash of an item key, which is what the keyed view indexes its rows by (§18.1, AD-13)
    template <>
    struct hash<albor::model::ItemKey>
    {
        [[nodiscard]] std::size_t operator()(const albor::model::ItemKey& key) const noexcept {
            // the text hashes, because the text is the whole of the key (§18.1, AD-13)
            return std::hash<std::string>{}(key.text());
        }
    };
} // namespace std
