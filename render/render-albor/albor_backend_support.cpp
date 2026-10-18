#include "render/render-albor/albor_backend_support.h"

namespace albor::render
{
    // the platform device is the metal one, which only exists where metal does (§5.3, AD-16)
    bool backend_device_available() {
        // a platform without a device of its own answers for the frame it cannot draw (§6.2)
        return false;
    }

    std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a platform with no device builds no surface, which the backend reports as unready (§6.2)
        (void)handle;
        (void)metrics;
        return nullptr;
    }
} // namespace albor::render
