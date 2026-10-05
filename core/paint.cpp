#include "core/paint.h"

namespace albor
{
    Color Color::rgb(float red, float green, float blue) {
        // an opaque colour is a colour whose alpha is one (§6.3)
        return rgba(red, green, blue, 1.0f);
    }

    Color Color::rgba(float red, float green, float blue, float alpha) {
        // every channel is carried through unchanged, because the range is the caller's contract (§6.3)
        return {.red = red, .green = green, .blue = blue, .alpha = alpha};
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
