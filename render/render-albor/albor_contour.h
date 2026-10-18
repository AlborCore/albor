#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/geometry.h"
#include "render/path_resource.h"

namespace albor::render
{
    // the stroke join the expansion resolves at a corner, which a stroke style names (§6.2)
    enum class Join
    {
        // the join extends to the miter point while it stays inside the limit
        Miter,

        // the join is filled with an arc of segments
        Round,

        // the join is filled with one flat cut
        Bevel,
    };

    // the stroke cap the open ends of a contour resolve to (§6.2)
    enum class Cap
    {
        // the stroke stops at the end point
        Butt,

        // the stroke ends in a half disc
        Round,

        // the stroke extends past the end point by the half width
        Square,
    };

    // the point flags the join calculation stamps, which the fill and the stroke expansions read
    enum ContourPointFlags : unsigned
    {
        // the contour turns a corner at the point, which two segments meet at
        Corner = 1u << 0,

        // the contour turns left at the point, which every point of a convex contour does
        Left = 1u << 1,

        // the outer join at the point widens past the miter limit, so it bevels
        Bevel = 1u << 2,

        // the inner side of the join overlaps itself, so it bevels as well
        InnerBevel = 1u << 3,
    };

    // one flattened point of a contour, which carries the direction and the miter the expansion reads
    struct ContourPoint
    {
        // the position in the space the contour was flattened in
        Point position;

        // the unit direction of the segment leaving this point, which wraps to the first point when closed
        float dx = 0.0f;
        float dy = 0.0f;

        // the length of the segment leaving this point
        float length = 0.0f;

        // the averaged inward normal of the join at this point, scaled by its reciprocal magnitude
        float miter_x = 0.0f;
        float miter_y = 0.0f;

        // the flags the join calculation stamped
        unsigned flags = 0;
    };

    // one flattened contour, which the fill and the stroke expansions consume (§6.4)
    struct Contour
    {
        // the points in drawing order, wound so the inward normal is the left one
        std::vector<ContourPoint> points;

        // whether the last point joins the first, which a Close verb or a repeated start stamps
        bool closed = false;

        // whether every turn leans the same way, which decides a fan fill against a stencilled one
        bool convex = false;
    };

    // flatten a verb stream into contours, which normalizes the winding and drops a repeated trailing point (§6.4)
    [[nodiscard]] std::vector<Contour> build_contours(std::span<const PathVerb> verbs, std::span<const Point> points);

    // stamp the unit direction of the segment leaving every point, which a hand built contour still needs
    void stamp_directions(Contour& contour);

    // compute the signed area of a contour with the winding formula the normalization uses
    [[nodiscard]] float polygon_area(const std::vector<ContourPoint>& points);

    // compute the miter directions, the join flags and the convexity the expansions read (nanovg technique)
    void calculate_joins(Contour& contour, float width, Join join, float miter_limit);
} // namespace albor::render
