#pragma once

#include <algorithm>

#include "core/paint.h"

namespace albor
{
    // interpolate between two scalars, which is the linear easing of §7.3
    [[nodiscard]] inline float lerp(float a, float b, float t) {
        // the result is the blend of the two ends, which is what a linear ramp produces (§7.3)
        return a + (b - a) * t;
    }

    // smooth the normalized time with a smooth step, which is the default easing of §7.3
    [[nodiscard]] inline float ease_in_out(float t) {
        // the progress is clamped, because an easing outside the unit interval overshoots the end (§7.3)
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        // the smooth step is the quadratic rise and fall, which starts and ends at zero speed (§7.3)
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    // interpolate between two colours component wise, which is what a colour animation needs (§7.3)
    [[nodiscard]] inline Color lerp(Color a, Color b, float t) {
        // the blend goes through the factory rather than the aggregate, because a t outside the unit range blends past an end and the factory is what keeps the result inside the srgb range (§6.3, §7.3)
        return Color::rgba(lerp(a.red, b.red, t), lerp(a.green, b.green, t), lerp(a.blue, b.blue, t), lerp(a.alpha, b.alpha, t));
    }
} // namespace albor
