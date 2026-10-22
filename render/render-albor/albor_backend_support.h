#pragma once

#include <memory>

#include "core/native_window_handle.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::render
{
    // report whether the platform graphics device came up, which every backend answers on its own (§6.3)
    [[nodiscard]] bool backend_device_available();

    // build the presentation surface over the native handle, which a null handle leaves offscreen (§6.2)
    [[nodiscard]] std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics);
} // namespace albor::render
