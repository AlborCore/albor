#include "render/render-albor/albor_contour.h"

#include <algorithm>
#include <cmath>

namespace albor::render
{
    namespace
    {
        // the tolerance two positions compare equal at, which drops the repeated trailing point a Close carries
        constexpr float POINT_TOLERANCE = 0.01f;

        // the largest scale the miter normal accepts, which stops a needle join from exploding (nanovg technique)
        constexpr float MITER_SCALE_LIMIT = 600.0f;

        // the squared magnitude below which a miter normal counts as zero, which a straight join carries
        constexpr float MITER_ZERO_LIMIT = 0.000001f;
    } // namespace

    void stamp_directions(Contour& contour) {
        // every point carries the unit direction of the segment leaving it, which wraps to the first point
        for (std::size_t index = 0; index < contour.points.size(); index++) {
            // the successor of the point wraps to the first one, which a closed contour joins through
            const std::size_t next = (index + 1) % contour.points.size();
            const Point& from = contour.points[index].position;
            const Point& to = contour.points[next].position;
            // the unnormalized direction and its length
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            // a zero length segment clears the direction, which a repeated point carries
            if (length > 0.0f) {
                contour.points[index].dx = dx / length;
                contour.points[index].dy = dy / length;
                contour.points[index].length = length;
            }
            else {
                contour.points[index].dx = 0.0f;
                contour.points[index].dy = 0.0f;
                contour.points[index].length = 0.0f;
            }
        }
    }

    float polygon_area(const std::vector<ContourPoint>& points) {
        // a contour with fewer than three points spans no area, which an open two point run carries
        if (points.size() < 3)
            return 0.0f;
        // the area fans every triangle from the first point, which is the winding formula the expansion normalizes to
        float area = 0.0f;
        const Point& a = points[0].position;
        for (std::size_t index = 2; index < points.size(); index++) {
            // the triangle spans from the first point over the previous one to this one
            const float ab_x = points[index - 1].position.x - a.x;
            const float ab_y = points[index - 1].position.y - a.y;
            const float ac_x = points[index].position.x - a.x;
            const float ac_y = points[index].position.y - a.y;
            // the winding formula adds the cross of the two edges
            area += ac_x * ab_y - ab_x * ac_y;
        }
        return area * 0.5f;
    }

    std::vector<Contour> build_contours(std::span<const PathVerb> verbs, std::span<const Point> points) {
        // the contours the walk produces, one per MoveTo
        std::vector<Contour> contours;
        // the point cursor walks the point list beside the verb list
        std::size_t cursor = 0;

        // every verb appends to the contour under construction, which a MoveTo opens and a Close seals
        for (const PathVerb verb : verbs) {
            switch (verb) {
            // a move opens a fresh contour at the next point
            case PathVerb::MoveTo:
                contours.emplace_back();
                if (cursor < points.size()) {
                    // the anchor point starts the contour
                    ContourPoint anchor;
                    anchor.position = points[cursor];
                    anchor.flags = Corner;
                    contours.back().points.push_back(anchor);
                    cursor++;
                }
                break;
            // a line appends the next point, opening a contour itself when no move preceded it
            case PathVerb::LineTo:
                if (contours.empty())
                    contours.emplace_back();
                if (cursor < points.size()) {
                    // a point repeating its predecessor merges flags, which keeps the segment nonzero (nanovg technique)
                    const Point candidate = points[cursor];
                    const bool repeats = !contours.back().points.empty() && std::abs(candidate.x - contours.back().points.back().position.x) <= POINT_TOLERANCE && std::abs(candidate.y - contours.back().points.back().position.y) <= POINT_TOLERANCE;
                    if (repeats) {
                        // the repeated point only adds its corner flag, which the join calculation reads
                        contours.back().points.back().flags |= Corner;
                    }
                    else {
                        // the reached point joins the contour, which every recorded point corners at
                        ContourPoint reached;
                        reached.position = candidate;
                        reached.flags = Corner;
                        contours.back().points.push_back(reached);
                    }
                    cursor++;
                }
                break;
            // a close seals the current contour, which the trailing point deduplication finishes below
            case PathVerb::Close:
                if (!contours.empty())
                    contours.back().closed = true;
                break;
            }
        }

        // every contour is normalized in place, because the winding decides which side the normals point to
        for (Contour& contour : contours) {
            // a contour with fewer than three points spans no area and carries no winding to normalize
            if (contour.points.size() >= 3) {
                // a trailing point that repeats the start is dropped, which the recorded Close produces (§6.4)
                const ContourPoint& first = contour.points.front();
                const ContourPoint& last = contour.points.back();
                const float dx = last.position.x - first.position.x;
                const float dy = last.position.y - first.position.y;
                if (std::abs(dx) <= POINT_TOLERANCE && std::abs(dy) <= POINT_TOLERANCE) {
                    contour.points.pop_back();
                    contour.closed = true;
                }
                // the winding is normalized to a positive area, which is the side the left normals point inward on
                if (contour.points.size() >= 3 && polygon_area(contour.points) < 0.0f)
                    std::reverse(contour.points.begin(), contour.points.end());
            }
            // every contour with a segment carries the directions the join math reads
            stamp_directions(contour);
            // the convexity is finished by the join calculation, which no joinless contour can claim
            contour.convex = false;
        }
        return contours;
    }

    void calculate_joins(Contour& contour, float width, Join join, float miter_limit) {
        // the joins need a width to measure against, which a zero width keeps at the zero normals
        const float reciprocal = width > 0.0f ? 1.0f / width : 0.0f;
        // a contour with fewer than three points carries no join to calculate
        if (contour.points.size() < 3) {
            contour.convex = false;
            return;
        }
        // the left turn count decides the convexity, which every point of a convex contour contributes
        std::size_t left_turns = 0;
        // every join reads the previous point and the point itself, which the walk pairs up
        ContourPoint* previous = &contour.points.back();
        for (ContourPoint& point : contour.points) {
            // the inward normals of the incoming and the outgoing segment
            const float normal0_x = previous->dy;
            const float normal0_y = -previous->dx;
            const float normal1_x = point.dy;
            const float normal1_y = -point.dx;
            // the averaged inward normal starts at half the sum
            point.miter_x = (normal0_x + normal1_x) * 0.5f;
            point.miter_y = (normal0_y + normal1_y) * 0.5f;
            // the squared magnitude decides whether the averaged normal needs rescaling
            const float magnitude_squared = point.miter_x * point.miter_x + point.miter_y * point.miter_y;
            if (magnitude_squared > MITER_ZERO_LIMIT) {
                // the normal is rescaled to unit length, with the scale capped for needle joins
                const float scale = std::min(1.0f / magnitude_squared, MITER_SCALE_LIMIT);
                point.miter_x *= scale;
                point.miter_y *= scale;
            }
            // the flags reset to the corner bit, which the move verb stamped
            point.flags = (point.flags & Corner) != 0u ? Corner : 0u;
            // the cross of the incoming and the outgoing direction decides the turn
            const float cross = point.dx * previous->dy - previous->dx * point.dy;
            if (cross > 0.0f) {
                // a left turn is stamped and counted
                point.flags |= Left;
                left_turns++;
            }
            // the inner side bevels when the join is sharper than the shorter segment can carry
            const float limit = std::max(1.01f, std::min(previous->length, point.length) * reciprocal);
            if (magnitude_squared * limit * limit < 1.0f)
                point.flags |= InnerBevel;
            // a corner bevels when the miter would exceed its limit or the join style asks for one
            if ((point.flags & Corner) != 0u) {
                const bool exceeds = magnitude_squared * miter_limit * miter_limit < 1.0f;
                if (exceeds || join == Join::Round || join == Join::Bevel)
                    point.flags |= Bevel;
            }
            // the previous point advances with the walk
            previous = &point;
        }
        // a contour whose every point turns left is convex, which the winding normalization guarantees
        contour.convex = left_turns == contour.points.size();
    }
} // namespace albor::render
