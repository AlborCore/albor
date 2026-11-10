#include "albor/widgets/scroll_wheel.h"

namespace albor
{
    float wheel_scale(const Event& event, float line_extent) {
        // a trackpad already counts points, so the delta is the distance; a notched wheel counts notches (§8.3)
        if (event.precision == ScrollPrecision::Point)
            return 1.0f;
        // the notched wheel moves a whole line, which is the policy the theme states (§8.3)
        return line_extent;
    }
} // namespace albor
