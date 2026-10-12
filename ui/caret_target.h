#pragma once

#include "core/geometry.h"
#include "core/text_range.h"

namespace albor
{
    // the careted editing surface a focused node exposes, which an input method anchors its candidates by (§8.3)
    class CaretTarget
    {
    public:
        virtual ~CaretTarget() = default;

        // hand out the caret rect in logical pixels, which an anchor may be resolved from (§8.3)
        [[nodiscard]] virtual Rect caret_rect() const = 0;
    };

    // the document a focused node edits, which an input method reads and replaces by range (§8.3)
    class TextInputTarget
    {
    public:
        virtual ~TextInputTarget() = default;

        // hand out the selection in utf16 units, which is the unit an input method addresses a document in (§8.3)
        [[nodiscard]] virtual TextInputRange selection_range() const = 0;
    };
} // namespace albor
