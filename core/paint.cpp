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

    bool Paint::fills() const {
        // a fully transparent fill contributes nothing, so the paint draws no fill at all (§6.2)
        return fill.alpha > 0.0f;
    }

    bool Paint::strokes() const {
        // a stroke needs both a visible colour and a positive width to reach the surface (§6.2)
        return stroke.alpha > 0.0f && stroke_width > 0.0f;
    }
} // namespace albor
