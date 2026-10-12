#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace albor
{
    // the half open span of a document an input method reads or replaces, counted in the utf16 units appkit speaks (§8.3)
    struct TextInputRange
    {
        // where the span begins, counted from the start of the document (§8.3)
        std::size_t location = 0;

        // how long the span is, which zero means a caret rather than a selection (§8.3)
        std::size_t length = 0;

        // compare two ranges field by field, which an event comparison needs (§8.3)
        friend bool operator==(const TextInputRange& lhs, const TextInputRange& rhs) = default;
    };

    // the utf16 offset a utf8 byte offset stands at, which is what an input method addresses the document in (§8.3)
    [[nodiscard]] std::size_t utf16_offset_for_byte(std::string_view text, std::size_t byte);

    // the utf8 byte offset a utf16 offset stands at, which is what the framework edits the document in (§8.3)
    [[nodiscard]] std::size_t byte_for_utf16_offset(std::string_view text, std::size_t utf16_offset);

    // the utf16 length of the whole document, which is what an input method measures its range against (§8.3)
    [[nodiscard]] std::size_t utf16_length(std::string_view text);
} // namespace albor