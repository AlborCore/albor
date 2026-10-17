#pragma once

#include <functional>
#include <memory>

#include "core/native_window_handle.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::blend2d
{
    // the signature a platform half implements to build the surface a window presents through (§5.3, AD-16)
    using WindowSurfaceFactory = std::function<std::unique_ptr<RenderSurface>(NativeWindowHandle, const WindowMetrics&)>;

    // install the window surface half of the backend, which a platform adapter calls at load (§5.3, AD-16)
    void set_window_surface_factory(WindowSurfaceFactory factory);

    // build the surface a window presents through, which falls back to the plain cpu raster (§6.3, AD-26)
    [[nodiscard]] std::unique_ptr<RenderSurface> create_window_surface(NativeWindowHandle handle, const WindowMetrics& metrics);
} // namespace albor::blend2d
