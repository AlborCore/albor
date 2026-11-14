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

        // build the overlap of two rectangles, which is what narrowing a clip to a rectangle settles for (§6.2)
        [[nodiscard]] Rect intersect(const Rect& other) const;

        // build the smallest rectangle covering both, which is how damage regions accumulate (§6.1)
        [[nodiscard]] Rect unioned(const Rect& other) const;

        // grow every edge by the amount, which covers a stroke or a shadow reach (§6.1)
        [[nodiscard]] Rect inflated(float amount) const;

        // compare two rectangles component wise
        friend bool operator==(const Rect& lhs, const Rect& rhs) = default;
    };

    // the four edge insets applied around content, in logical pixels (§8.2)
    struct Insets
    {
        // the inset above the content in logical pixels (§8.2)
        float top = 0.0f;

        // the inset to the right of the content in logical pixels (§8.2)
        float right = 0.0f;

        // the inset below the content in logical pixels (§8.2)
        float bottom = 0.0f;

        // the inset to the left of the content in logical pixels (§8.2)
        float left = 0.0f;

        // report whether the insets consume no space at all, which is what an unconfigured inset carries (§8.2)
        [[nodiscard]] bool empty() const;

        // compare two inset sets field by field
        friend bool operator==(const Insets& lhs, const Insets& rhs) = default;
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

        // map a rectangle through the transform, returning the rectangle that covers the mapped corners (§6.2)
        [[nodiscard]] Rect map(const Rect& rect) const;

        // the transform applying this one first and the argument second, how a retained draw joins (§6.2, §6.4)
        [[nodiscard]] Transform then(const Transform& outer) const;

        // the transform reversing this one, which the identity answers when the mapping collapses (§6.2)
        [[nodiscard]] Transform inverse() const;

        // compare two transforms component wise
        friend bool operator==(const Transform& lhs, const Transform& rhs) = default;
    };
} // namespace albor
