#include "core/geometry.h"

#include <algorithm>

namespace albor
{
    bool Rect::empty() const {
        // report whether the rectangle has no area at all
        return width <= 0.0f || height <= 0.0f;
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
