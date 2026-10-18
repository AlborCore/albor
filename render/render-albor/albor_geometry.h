#pragma once

#include "core/geometry.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_contour.h"

namespace albor::render
{
    // the device pixel fringe the antialiasing ramps across, which every emission measures in (nanovg technique)
    inline constexpr float FRINGE = 1.0f;

    // the miter limit the fill fringe joins with, which the nanovg technique fixes (§6.2)
    inline constexpr float FILL_MITER_LIMIT = 2.4f;

    // invert a transform, which the scissor needs to map a raster position back into scene space
    [[nodiscard]] Transform invert_transform(const Transform& transform);

    // build the contour of a rectangle, wound so the left normals point inward (§6.2)
    [[nodiscard]] Contour rect_contour(const Rect& rect);

    // build the contour of a rounded rectangle, whose corners are arcs the tessellation tolerance divides (§6.2)
    [[nodiscard]] Contour rounded_rect_contour(const Rect& rect, float radius, float tess_tolerance);

    // the segments one arc of the given radius and sweep uses, which the tessellation tolerance decides
    [[nodiscard]] int curve_divs(float radius, float arc, float tess_tolerance);

    // append the fill of a device space contour, whose fringe is one device pixel (nanovg technique)
    [[nodiscard]] EmittedRange expand_fill(FrameBatch& sink, Contour& contour);

    // append a device space stroke, handing back the alpha a sub fringe width emulates (nanovg technique)
    [[nodiscard]] StrokeEmission expand_stroke(FrameBatch& sink, Contour& contour, float half_width, Cap cap, Join join, float miter_limit, float tess_tolerance);
} // namespace albor::render
