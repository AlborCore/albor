#include "albor/ui/tooltip.h"

#include <algorithm>

namespace albor
{
    Rect place_tooltip(Rect anchor, Size size, Rect viewport, float gap, float margin) {
        // the bubble is centred under the anchor, which is the conventional placement (§8.3)
        float x = anchor.x + (anchor.width - size.width) * 0.5f;
        // the bubble hangs below the anchor by the style's gap, which keeps the control readable (§8.3)
        float y = anchor.y + anchor.height + gap;
        // a bubble that would leave the bottom flips above the anchor, which is the first collision rule (§8.3)
        if (y + size.height > viewport.y + viewport.height - margin)
            y = anchor.y - gap - size.height;
        // the horizontal range keeps the bubble inside the view, so a control beside the edge still reads (§8.3)
        const float left = viewport.x + margin;
        const float right = viewport.x + viewport.width - margin - size.width;
        // a view narrower than the bubble leaves the leading inset, because clamping would invert the range (§8.3)
        x = right < left ? left : std::clamp(x, left, right);
        // the vertical range clamps as well, because a flipped bubble can still leave a short window (§8.3)
        const float top = viewport.y + margin;
        const float bottom = viewport.y + viewport.height - margin - size.height;
        // a view shorter than the bubble pins the top, which keeps the text's first line visible (§8.3)
        y = bottom < top ? top : std::clamp(y, top, bottom);
        // the placed rect carries the size it was measured at, which is what the overlay arranges to (§8.2)
        return Rect{.x = x, .y = y, .width = size.width, .height = size.height};
    }
} // namespace albor
