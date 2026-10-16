#include "core/paint.h"

#include <algorithm>

namespace albor
{
    Color Color::rgb(float red, float green, float blue) {
        // an opaque colour is a colour whose alpha is one (§6.3)
        return rgba(red, green, blue, 1.0f);
    }

    Color Color::rgba(float red, float green, float blue, float alpha) {
        // every channel is clamped into the srgb range, because a binding computed from data can land outside it (§6.3)
        return {.red = std::clamp(red, 0.0f, 1.0f), .green = std::clamp(green, 0.0f, 1.0f), .blue = std::clamp(blue, 0.0f, 1.0f), .alpha = std::clamp(alpha, 0.0f, 1.0f)};
    }

    Color Color::with_alpha(float opacity) const {
        // the channels are untouched, because an opacity dims coverage rather than hue (§6.3)
        return Color::rgba(red, green, blue, opacity * alpha);
    }

    Color Color::alpha_set(float opacity) const {
        // the alpha is replaced rather than scaled, which is what an explicit opacity asks for (§6.3)
        return Color::rgba(red, green, blue, opacity);
    }

    Color Color::transparentized(float fraction) const {
        // the alpha moves toward zero by the fraction, which is what a de-emphasis reads as (§6.3)
        return Color::rgba(red, green, blue, alpha * (1.0f - std::clamp(fraction, 0.0f, 1.0f)));
    }

    bool Paint::fills() const {
        // a fully transparent fill contributes nothing, so the paint draws no fill at all (§6.2)
        return fill.alpha > 0.0f;
    }

    bool Paint::strokes() const {
        // a stroke needs both a visible colour and a positive width to reach the surface (§6.2)
        return stroke.alpha > 0.0f && stroke_width > 0.0f;
    }

    bool Shadow::casts() const {
        // a clear colour casts nothing, and a negative blur is not a blur (§6.2)
        return color.alpha > 0.0f && blur >= 0.0f;
    }

    bool Shadow::empty() const {
        // the default shadow carries no colour, which is the only way to say there is none (§6.2)
        return color.alpha <= 0.0f;
    }
} // namespace albor
