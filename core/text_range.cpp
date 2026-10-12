#include "core/text_range.h"

namespace albor
{
    namespace
    {
        // the utf16 units one utf8 character occupies, which is one for everything below the astral plane (§8.3)
        std::size_t utf16_units(unsigned char lead) {
            // a four byte form is a surrogate pair in utf16, so it is the only shape that counts twice (§8.3)
            if ((lead & 0xF8U) == 0xF0U)
                return 2;
            // every other form is one utf16 unit, because the basic plane spells them with one (§8.3)
            return 1;
        }

        // the length of the character a lead byte opens, which is what a walk steps over (§8.3)
        std::size_t utf8_length(unsigned char lead) {
            // a four byte form is the longest one the utf8 rules admit (§8.3)
            if ((lead & 0xF8U) == 0xF0U)
                return 4;
            // a three byte form follows it (§8.3)
            if ((lead & 0xF0U) == 0xE0U)
                return 3;
            // a two byte form follows that (§8.3)
            if ((lead & 0xE0U) == 0xC0U)
                return 2;
            // a continuation byte standing alone is read as ascii, which is what the rules say (§8.3)
            return 1;
        }
    } // namespace

    std::size_t utf16_offset_for_byte(std::string_view text, std::size_t byte) {
        // the offset past the end is the length of the whole document, which is where a caret clamps (§8.3)
        const std::size_t limit = std::min(byte, text.size());
        // the units counted so far, which every character adds its own share of (§8.3)
        std::size_t units = 0;
        // the walk steps a character at a time, because a character is the unit both encodings agree on (§8.3)
        for (std::size_t index = 0; index < limit;) {
            // the character at the cursor contributes its units (§8.3)
            units += utf16_units(static_cast<unsigned char>(text[index]));
            // the cursor steps the whole character, so it never lands inside one (§8.3)
            index += utf8_length(static_cast<unsigned char>(text[index]));
        }
        return units;
    }

    std::size_t byte_for_utf16_offset(std::string_view text, std::size_t utf16_offset) {
        // the units counted so far, which every character adds its own share of (§8.3)
        std::size_t units = 0;
        // the walk steps a character at a time, because a character is the unit both encodings agree on (§8.3)
        for (std::size_t index = 0; index < text.size();) {
            // an offset inside a character clamps to that character's start, which is where a caret can stand (§8.3)
            if (units + utf16_units(static_cast<unsigned char>(text[index])) > utf16_offset)
                return index;
            // the units this character took are counted, so the next one is measured against (§8.3)
            units += utf16_units(static_cast<unsigned char>(text[index]));
            // the cursor steps the whole character, so it never lands inside one (§8.3)
            index += utf8_length(static_cast<unsigned char>(text[index]));
        }
        // an offset past the end lands at the end, because a caret can stand there (§8.3)
        return text.size();
    }

    std::size_t utf16_length(std::string_view text) {
        // the length is the offset of the end, which is the one walk that answers it (§8.3)
        return utf16_offset_for_byte(text, text.size());
    }
} // namespace albor