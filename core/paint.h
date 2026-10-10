#pragma once

namespace albor
{
    // a straight srgb colour with components in the zero to one range
    struct Color
    {
        // the red component
        float red = 0.0f;

        // the green component
        float green = 0.0f;

        // the blue component
        float blue = 0.0f;

        // the alpha component, where one is fully opaque
        float alpha = 1.0f;

        // build an opaque colour, with every channel clamped into the unit range (§6.3)
        [[nodiscard]] static Color rgb(float red, float green, float blue);

        // build a colour with an explicit alpha, with every channel clamped into the unit range (§6.3)
        [[nodiscard]] static Color rgba(float red, float green, float blue, float alpha);

        // compare two colours component wise
        friend bool operator==(const Color& lhs, const Color& rhs) = default;
    };

    // how a primitive is filled and stroked, which is the drawing contract of a RenderContext (§6.2)
    struct Paint
    {
        // the fill colour of the primitive
        Color fill = Color::rgb(0.0f, 0.0f, 0.0f);

        // the stroke colour of the primitive
        Color stroke = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);

        // the stroke width in logical pixels
        float stroke_width = 1.0f;

        // report whether the paint fills its primitive
        [[nodiscard]] bool fills() const;

        // report whether the paint strokes its primitive
        [[nodiscard]] bool strokes() const;

        // compare two paints component wise
        friend bool operator==(const Paint& lhs, const Paint& rhs) = default;
    };

    // the stroke description a batched line primitive carries (§6.4)
    struct StrokeStyle
    {
        // the stroke colour
        Color color = Color::rgb(0.0f, 0.0f, 0.0f);

        // the stroke width in logical pixels
        float width = 1.0f;

        // compare two stroke styles component wise
        friend bool operator==(const StrokeStyle& lhs, const StrokeStyle& rhs) = default;
    };
} // namespace albor
