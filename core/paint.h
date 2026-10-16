#pragma once

#include "core/geometry.h"

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

        // scale the alpha by a fraction, which is what an opacity and a fade are (§6.3)
        [[nodiscard]] Color with_alpha(float alpha) const;

        // replace the alpha outright, which is what a fill at a chosen opacity is (§6.3)
        [[nodiscard]] Color alpha_set(float alpha) const;

        // scale the alpha down by a fraction, which is what a de-emphasis tint is (§6.3)
        [[nodiscard]] Color transparentized(float fraction) const;

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

    // the shadow a surface casts, which needs an offscreen pass and a composite rather than a second draw (§6.2)
    struct Shadow
    {
        // the colour of the cast, where a clear colour casts nothing (§6.2)
        Color color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);

        // how far the cast falls from the shape, which is what lifts it off the surface (§6.2)
        Point offset{0.0f, 2.0f};

        // how soft the cast edge is, which is the blur radius the offscreen pass runs (§6.2)
        float blur = 4.0f;

        // report whether the shadow would put any ink down, which a recorder culls on (§6.2)
        [[nodiscard]] bool casts() const;

        // report whether the shadow is the default one, which is no shadow at all (§6.2)
        [[nodiscard]] bool empty() const;

        // compare two shadows component wise
        friend bool operator==(const Shadow& lhs, const Shadow& rhs) = default;
    };
} // namespace albor
