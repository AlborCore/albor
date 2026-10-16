#include "render/render-blend2d/blend2d_renderer_backend.h"

#include "render/render-blend2d/blend2d_render_surface.h"
#include "render/render-blend2d/blend2d_window_surface.h"
#include "render/renderer_backend.h"

namespace albor::blend2d
{
    Blend2DRendererBackend::Blend2DRendererBackend() = default;

    Blend2DRendererBackend::~Blend2DRendererBackend() = default;

    bool Blend2DRendererBackend::ready() const {
        // the blend2d rasterizer needs no device, no layer and no display, so it is always ready (§6.3, AD-26)
        return true;
    }

    std::unique_ptr<RenderSurface> Blend2DRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // the surface is refused rather than half built when the backend is not ready (§6.2)
        if (!ready())
            return nullptr;
        // a window carries the native layer the platform half presents through, which is the windowed path (§5.3, AD-16)
        if (handle.valid())
            return create_window_surface(handle, metrics);
        // the metrics alone decide the raster extent, which is what makes the surface reproducible (§6.3)
        return Blend2DRenderSurface::create(metrics);
    }

    void Blend2DRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!surface.valid())
            return;
        // metrics with no area cannot be rasterized, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // the extent is written here, on the thread that owns the surface (§5.3, D9.1)
        surface.resize(metrics);
    }

    void Blend2DRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // a frame needs a surface that could draw (§6.1)
        if (!surface.valid())
            return;
        // a scene at stale metrics is refused rather than repaired here, exactly as the skia backend does (§6.1, D9.1)
        if (surface.metrics() != scene.metrics())
            return;
        // stage two begins the frame and draws the immutable scene into it (§6.1, AD-18)
        RenderContext& context = surface.begin_frame();
        // the background is a full surface rectangle, which is the clear of phase 1 (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
        context.draw_rect(full, {.fill = scene.background()});
        // the commands replay through the one shared replay, which is what the skia backend also calls (§6.1, §6.2)
        replay_commands(scene.commands(), context, full);
        // the frame finishes once the immutable scene is fully recorded (§6.1)
        surface.end_frame();
    }

    void install_blend2d_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<Blend2DRendererBackend>(); });
    }
} // namespace albor::blend2d
