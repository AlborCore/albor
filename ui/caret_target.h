#pragma once

#include "core/geometry.h"

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
} // namespace albor
