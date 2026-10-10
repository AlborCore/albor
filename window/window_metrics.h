#pragma once

#include "core/geometry.h"

namespace albor
{
    // the window facts the framework treats as authoritative, with px denoting logical and phx physical pixels (AD-27)
    struct WindowMetrics
    {
        // the logical size the layout system measures against
        Size logical_size;

        // the physical pixel size the renderer presents
        Size pixel_size;

        // the ratio between the physical and the logical size
        float scale_factor = 1.0f;

        // derive the physical size from a logical size and a scale factor
        [[nodiscard]] static WindowMetrics from_logical(Size logical_size, float scale_factor);

        // report whether the metrics describe a usable surface
        [[nodiscard]] bool valid() const;

        // compare two metrics component wise
        friend bool operator==(const WindowMetrics& lhs, const WindowMetrics& rhs) = default;
    };
} // namespace albor
