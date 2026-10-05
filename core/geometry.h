#pragma once

namespace albor
{
    // a logical pixel extent, where px denotes logical pixels and phx physical ones (AD-27)
    struct Size
    {
        // the horizontal extent in logical pixels
        float width = 0.0f;

        // the vertical extent in logical pixels
        float height = 0.0f;

        // compare two extents component wise
        friend bool operator==(const Size& lhs, const Size& rhs) = default;
    };

    // a logical pixel position, in the same coordinate space as a Size
    struct Point
    {
        // the horizontal coordinate in logical pixels
        float x = 0.0f;

        // the vertical coordinate in logical pixels
        float y = 0.0f;

        // compare two positions component wise
        friend bool operator==(const Point& lhs, const Point& rhs) = default;
    };

    // an axis aligned logical pixel rectangle
    struct Rect
    {
        // the left edge in logical pixels
        float x = 0.0f;

        // the top edge in logical pixels
        float y = 0.0f;

        // the horizontal extent in logical pixels
        float width = 0.0f;

        // the vertical extent in logical pixels
        float height = 0.0f;

        // report whether the rectangle has no area at all
        [[nodiscard]] bool empty() const;

        // build a rectangle from an origin and an extent
        [[nodiscard]] static Rect from_origin_size(Point origin, Size size);

        // compare two rectangles component wise
        friend bool operator==(const Rect& lhs, const Rect& rhs) = default;
    };

    // the affine transform a render context applies to its drawing commands (§6.2)
    struct Transform
    {
        // the horizontal scale factor
        float scale_x = 1.0f;

        // the horizontal skew factor
        float skew_x = 0.0f;

        // the horizontal translation in logical pixels
        float translate_x = 0.0f;

        // the vertical skew factor
        float skew_y = 0.0f;

        // the vertical scale factor
        float scale_y = 1.0f;

        // the vertical translation in logical pixels
        float translate_y = 0.0f;

        // build a pure translation
        [[nodiscard]] static Transform translation(float x, float y);

        // build a pure scale
        [[nodiscard]] static Transform scaling(float scale_x, float scale_y);

        // map a point through the transform
        [[nodiscard]] Point apply(const Point& point) const;

        // compare two transforms component wise
        friend bool operator==(const Transform& lhs, const Transform& rhs) = default;
    };
} // namespace albor
