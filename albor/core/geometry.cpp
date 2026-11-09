#include "albor/core/geometry.h"

#include <algorithm>
#include <cmath>

namespace albor
{
    bool Rect::empty() const {
        // report whether the rectangle has no area at all
        return width <= 0.0f || height <= 0.0f;
    }

    bool Insets::empty() const {
        // an inset set that consumes nothing on every edge is the unconfigured one (§8.2)
        return top == 0.0f && right == 0.0f && bottom == 0.0f && left == 0.0f;
    }

    Rect Rect::from_origin_size(Point origin, Size size) {
        // build a rectangle from an origin and an extent
        return {.x = origin.x, .y = origin.y, .width = size.width, .height = size.height};
    }

    Rect Rect::intersect(const Rect& other) const {
        // the leading edges sit where the two rectangles overlap, so the result starts at the later of the two (§6.2)
        const float left = std::max(x, other.x);
        // the same rule applies on the vertical axis, because an overlap starts at the later top edge (§6.2)
        const float top = std::max(y, other.y);
        // the trailing edge is the earlier of the two, because the overlap ends where the first rectangle does (§6.2)
        const float right = std::min(x + width, other.x + other.width);
        // the same rule applies on the vertical axis, for the same reason (§6.2)
        const float bottom = std::min(y + height, other.y + other.height);
        // a rectangle that only touches has no area, which the clamp reports as empty rather than as a sliver (§6.2)
        return Rect::from_origin_size(Point{.x = left, .y = top}, Size{.width = std::max(0.0f, right - left), .height = std::max(0.0f, bottom - top)});
    }

    Rect Rect::unioned(const Rect& other) const {
        // an empty rectangle adds nothing, so the other one is the whole union (§6.1)
        if (empty())
            return other;
        // an empty rectangle beside a real one leaves the real one unchanged (§6.1)
        if (other.empty())
            return *this;
        // the union starts at the earlier leading edge on each axis (§6.1)
        const float left = std::min(x, other.x);
        // the top edge follows the same rule, because the union covers both (§6.1)
        const float top = std::min(y, other.y);
        // the trailing edge is the later of the two, because the union covers both (§6.1)
        const float right = std::max(x + width, other.x + other.width);
        // the bottom edge follows the same rule (§6.1)
        const float bottom = std::max(y + height, other.y + other.height);
        // the result is one rectangle over both, in the same space the inputs share (§6.1)
        return Rect::from_origin_size(Point{.x = left, .y = top}, Size{.width = right - left, .height = bottom - top});
    }

    Rect Rect::inflated(float amount) const {
        // an empty rectangle has no ink to grow, so it stays empty (§6.1)
        if (empty())
            return *this;
        // the amount grows every edge, so the origin moves back and both extents gain twice it (§6.1)
        return Rect::from_origin_size(Point{.x = x - amount, .y = y - amount}, Size{.width = width + amount * 2.0f, .height = height + amount * 2.0f});
    }

    Transform Transform::translation(float x, float y) {
        // build a pure translation
        return {.translate_x = x, .translate_y = y};
    }

    Transform Transform::scaling(float scale_x, float scale_y) {
        // build a pure scale
        return {.scale_x = scale_x, .scale_y = scale_y};
    }

    Point Transform::apply(const Point& point) const {
        // the horizontal result combines scale, skew and translation in that order
        const float mapped_x = scale_x * point.x + skew_x * point.y + translate_x;
        // the vertical result combines skew, scale and translation in that order
        const float mapped_y = skew_y * point.x + scale_y * point.y + translate_y;
        // map a point through the transform
        return {.x = mapped_x, .y = mapped_y};
    }

    Rect Transform::map(const Rect& rect) const {
        // an empty rectangle has no corner to place, so it stays empty (§6.2)
        if (rect.empty())
            return rect;
        // the origin maps through the transform, which is where the top left corner lands (§6.2)
        const Point origin = apply(Point{.x = rect.x, .y = rect.y});
        // the width edge is placed by the horizontal scale, which is exact for a pure scale (§6.2)
        const float width_on_x = rect.width * scale_x;
        // the height edge reaches horizontally through the skew, because a shear turns it (§6.2)
        const float height_on_x = rect.height * skew_x;
        // the width edge reaches vertically through the other skew (§6.2)
        const float width_on_y = rect.width * skew_y;
        // the height edge is placed by the vertical scale (§6.2)
        const float height_on_y = rect.height * scale_y;
        // the left edge is where the origin lands plus whatever either edge reaches left (§6.2)
        const float left = origin.x + std::min(0.0f, width_on_x) + std::min(0.0f, height_on_x);
        // the top edge follows the same rule on the other axis (§6.2)
        const float top = origin.y + std::min(0.0f, width_on_y) + std::min(0.0f, height_on_y);
        // the width covers both horizontal reaches without subtracting them, which keeps a scale exact (§6.2)
        const float width = std::abs(width_on_x) + std::abs(height_on_x);
        // the height covers both vertical reaches the same way (§6.2)
        const float height = std::abs(width_on_y) + std::abs(height_on_y);
        // the edges become the rectangle that covers everything the transform places (§6.2)
        return Rect::from_origin_size(Point{.x = left, .y = top}, Size{.width = width, .height = height});
    }

    Transform Transform::then(const Transform& outer) const {
        // the result is the matrix product outer times this, so a point runs through this transform first (§6.4)
        Transform composed;
        // the first row multiplies the outer row against this transform's columns (§6.4)
        composed.scale_x = outer.scale_x * scale_x + outer.skew_x * skew_y;
        composed.skew_x = outer.scale_x * skew_x + outer.skew_x * scale_y;
        composed.translate_x = outer.scale_x * translate_x + outer.skew_x * translate_y + outer.translate_x;
        // the second row multiplies the outer row against this transform's columns the same way (§6.4)
        composed.skew_y = outer.skew_y * scale_x + outer.scale_y * skew_y;
        composed.scale_y = outer.skew_y * skew_x + outer.scale_y * scale_y;
        composed.translate_y = outer.skew_y * translate_x + outer.scale_y * translate_y + outer.translate_y;
        // hand back the composed transform
        return composed;
    }
} // namespace albor
