#include "window/window_metrics.h"

namespace albor
{
    WindowMetrics WindowMetrics::from_logical(Size logical_size, float scale_factor) {
        // the pixel size is the logical size scaled onto the device, which is what the renderer presents (AD-27)
        return {.logical_size = logical_size, .pixel_size = Size{logical_size.width * scale_factor, logical_size.height * scale_factor}, .scale_factor = scale_factor};
    }

    bool WindowMetrics::valid() const {
        // a usable surface needs a positive logical area, a positive scale and a positive physical area
        if (logical_size.width <= 0.0f || logical_size.height <= 0.0f)
            return false;
        // a non positive scale factor would invert the logical to physical mapping
        if (scale_factor <= 0.0f)
            return false;
        // a zero physical area means the platform has not sized the drawable yet
        return pixel_size.width > 0.0f && pixel_size.height > 0.0f;
    }
} // namespace albor
