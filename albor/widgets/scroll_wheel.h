#pragma once

#include "albor/ui/ui_event.h"

namespace albor
{
    // the distance one wheel notch moves a viewport when no theme names one, in logical pixels (§8.3)
    inline constexpr float SCROLL_LINE_EXTENT = 40.0f;

    // hand out the distance one wheel event moves a viewport, which every scrollable widget shares (§8.3)
    [[nodiscard]] float wheel_scale(const Event& event, float line_extent);
} // namespace albor
