#pragma once

#include "core/geometry.h"

namespace albor
{
    // the clip a recorder or a context stands at, which a rectangle and a corner radius describe (§6.2)
    struct ClipState
    {
        // the axis aligned area the clip keeps, which every clip narrows (§6.2)
        Rect rect;

        // the corner radius of the clip, which zero leaves it rectangular (§6.2)
        float radius = 0.0f;
    };

    // narrow a clip to a rectangle, keeping a radius only where the rectangle still contains the rounded region (§6.2)
    void clip_to_rect(ClipState& clip, const Rect& rect);

    // narrow a clip to a rounded rectangle, which governs only where it lies inside the current clip (§6.2)
    void clip_to_rounded_rect(ClipState& clip, const Rect& rect, float radius);
} // namespace albor
