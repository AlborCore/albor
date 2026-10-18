#include "render/clip_state.h"

#include <algorithm>

namespace albor
{
    namespace
    {
        // report whether the outer rectangle covers the inner one entirely (§6.2)
        bool covers(const Rect& outer, const Rect& inner) {
            // the four edges decide the containment, which a tolerance absorbs at the boundary
            const bool left = outer.x <= inner.x + 0.0001f;
            const bool top = outer.y <= inner.y + 0.0001f;
            const bool right = outer.x + outer.width >= inner.x + inner.width - 0.0001f;
            const bool bottom = outer.y + outer.height >= inner.y + inner.height - 0.0001f;
            return left && top && right && bottom;
        }

        // clamp a radius to half the shorter side, which a larger one cannot exceed (§6.2)
        float clamped_radius(const Rect& rect, float radius) {
            // a negative radius is no radius at all, which a caller may still ask for
            return std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
        }
    } // namespace

    void clip_to_rect(ClipState& clip, const Rect& rect) {
        // a rectangle containing the rounded region keeps its radius, which the narrower area then rounds (§6.2)
        const bool keeps = clip.radius > 0.0f && covers(rect, clip.rect);
        // the clip narrows to the overlap, because a nested clip may only ever cut more away (§6.2)
        clip.rect = clip.rect.intersect(rect);
        // a rectangle cutting the rounded region cannot express it, so the mask falls back to the rectangle (§6.2)
        clip.radius = keeps ? clamped_radius(clip.rect, clip.radius) : 0.0f;
    }

    void clip_to_rounded_rect(ClipState& clip, const Rect& rect, float radius) {
        // a rounded clip inside the current one governs the corners, which the containment decides (§6.2)
        const bool contained = covers(clip.rect, rect);
        // a rounded clip that contains the current one leaves the current corners in charge (§6.2)
        const bool contains = covers(rect, clip.rect);
        // the clip narrows to the overlap, because a nested clip may only ever cut more away (§6.2)
        clip.rect = clip.rect.intersect(rect);
        // overlapping rounded clips have no rounded rectangle to name, so the mask falls back to the rectangle (§6.2)
        if (contained)
            clip.radius = clamped_radius(clip.rect, radius);
        else if (!contains)
            clip.radius = 0.0f;
        else
            clip.radius = clamped_radius(clip.rect, clip.radius);
    }
} // namespace albor
