#pragma once

#include <span>
#include <vector>

#include "core/geometry.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_contour.h"

namespace albor::render
{
    // one closed ring the sweep fills, which carries the winding its contour was normalized from (§6.2)
    struct FillRing
    {
        // the ring positions, in the normalized order the join calculation produced
        std::vector<Point> points;

        // whether the normalization reversed the ring, which inverts the winding it contributes
        bool reversed = false;
    };

    // one trapezoid the fill sweep covers, between two consecutive rows of vertices (§6.2)
    struct FillTrapezoid
    {
        // the top row of the slab, in the space the rings were swept in
        float top = 0.0f;

        // the bottom row of the slab, which the trapezoid stops at
        float bottom = 0.0f;

        // the left edge at the two rows, which a sloped edge interpolates across the slab
        float left_top = 0.0f;
        float left_bottom = 0.0f;

        // the right edge at the two rows, which a sloped edge interpolates across the slab
        float right_top = 0.0f;
        float right_bottom = 0.0f;
    };

    // sweep closed rings into the trapezoids a non zero fill covers, which cuts the holes they wind around (§6.2)
    [[nodiscard]] std::vector<FillTrapezoid> sweep_fill(std::span<const FillRing> rings);

    // append the fill of a device space contour group, whose sweep cuts every hole the group carries (§6.2)
    [[nodiscard]] EmittedRange expand_fill_group(FrameBatch& sink, std::vector<Contour>& contours);
} // namespace albor::render
