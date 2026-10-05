#include "core/geometry.h"

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
} // namespace albor
