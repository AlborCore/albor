#include "albor/widgets/scroll_thumb.h"

#include <algorithm>

namespace albor
{
    Rect scroll_thumb_rect(float offset, float maximum, const Rect& track, float minimum) {
        // the travel a thumb can take is the track, which is what the share and the placement are read against (§8.2)
        const float travel = track.height;
        // the thumb takes the share of the track the viewport takes of the content, which is what the ratio means (§8.2)
        const float share = std::clamp(travel / std::max(1.0f, maximum + travel), 0.0f, 1.0f);
        // the thumb never shrinks below the grabbable floor, which is what keeps a long content reachable (§8.2)
        const float height = std::min(travel, std::max(minimum, travel * share));
        // the fraction is zero while there is nothing to scroll, so the thumb stays at the leading edge (§8.2)
        const float fraction = maximum > 0.0f ? std::clamp(offset / maximum, 0.0f, 1.0f) : 0.0f;
        // the thumb sits where the offset puts it, which is the whole of what a bar reports (§8.2)
        return Rect::from_origin_size(Point{.x = track.x, .y = track.y + (track.height - height) * fraction}, Size{.width = track.width, .height = height});
    }
} // namespace albor
