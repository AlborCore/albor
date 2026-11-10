#pragma once

#include "albor/core/geometry.h"

namespace albor
{
    // how wide a bar is drawn when no theme names one, in logical pixels (§6.1)
    inline constexpr float SCROLLBAR_EXTENT = 10.0f;

    // the smallest thumb a bar draws when no theme names one, which keeps a long content grabbable (§6.1)
    inline constexpr float SCROLLBAR_THUMB_MINIMUM = 24.0f;

    // hand out the strip a thumb covers in a track, which is the travel a bar reports (§8.2, §6.1)
    [[nodiscard]] Rect scroll_thumb_rect(float offset, float maximum, const Rect& track, float minimum);
} // namespace albor
