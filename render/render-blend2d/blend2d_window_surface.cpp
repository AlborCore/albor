#include "render/render-blend2d/blend2d_window_surface.h"

#include <memory>
#include <utility>

#include "render/render-blend2d/blend2d_render_surface.h"

namespace albor::blend2d
{
    namespace
    {
        // the state the installed factory lives in, heap held behind a constant initialized pointer (§5.3)
        struct WindowSurfaceFactoryState
        {
            // the installed factory, which is empty until a platform half installs one (§5.3)
            WindowSurfaceFactory factory;
        };

        // the state itself, sitting in heap exactly where the load time installer reaches it (§5.3)
        [[nodiscard]] WindowSurfaceFactoryState& factory_state() {
            static WindowSurfaceFactoryState* state = new WindowSurfaceFactoryState();
            return *state;
        }
    } // namespace

    void set_window_surface_factory(WindowSurfaceFactory factory) {
        // the factory is what a windowed host replaces with its presentation, so the raster stays the default (§5.3)
        factory_state().factory = std::move(factory);
    }

    std::unique_ptr<RenderSurface> create_window_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a platform half installed its presentation, so linking chooses how a window draws (§5.3, AD-16)
        if (factory_state().factory)
            return factory_state().factory(handle, metrics);
        // there is no platform window half in this build, so the raster draws with nothing to present into (§6.3, AD-26)
        (void)handle;
        return Blend2DRenderSurface::create(metrics);
    }
} // namespace albor::blend2d
