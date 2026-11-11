#include "albor/compiler/naming.h"

#include <cctype>

namespace albor::compiler
{
    std::string snake_case(std::string_view name) {
        // the letters accumulate, with an underscore inserted at each word boundary (§14.1)
        std::string out;
        // each letter is walked, because an upper case letter starts a word (§14.1)
        for (std::size_t index = 0; index < name.size(); index++) {
            // the letter is read once, so the checks below look at one byte (§14.1)
            const char letter = name[index];
            // a word starts after lower text or a digit, except inside a run of capitals (§14.1)
            const bool upper = letter >= 'A' && letter <= 'Z';
            // an upper letter is only a boundary when one exists before it (§14.1)
            if (upper && index != 0) {
                // the previous letter decides whether this one starts a word (§14.1)
                const char previous = name[index - 1];
                // lower text or a digit always ends the word before (§14.1)
                const bool after_text = (previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9');
                // a capital before a lower letter splits an acronym, which keeps the tail a word (§14.1)
                const bool split_acronym = previous >= 'A' && previous <= 'Z' && index + 1 < name.size() && name[index + 1] >= 'a' && name[index + 1] <= 'z';
                // the boundary exists after text or before the tail of an acronym (§14.1)
                if (after_text || split_acronym)
                    out += '_';
            }
            // the letter is lowered, because the file base is lower case (§14.1)
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
        }
        return out;
    }
} // namespace albor::compiler
