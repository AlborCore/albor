#include "render/render-software/software_renderer_backend.h"

#include "render/render-software/software_render_surface.h"
#include "render/renderer_backend.h"

namespace albor::software
{
    SoftwareRendererBackend::SoftwareRendererBackend() = default;

    SoftwareRendererBackend::~SoftwareRendererBackend() = default;

    bool SoftwareRendererBackend::ready() const {
        // the cpu rasterizer needs no device, no layer and no display, so it is always ready (§6.3, AD-26)
        return true;
    }

    std::unique_ptr<RenderSurface> SoftwareRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // the handle names no platform object here, because a cpu raster has no window to attach to (§6.2, AD-26)
        (void)handle;
        // the surface is refused rather than half built when the backend is not ready (§6.2)
        if (!ready())
            return nullptr;
        // the metrics alone decide the raster extent, which is what makes the surface reproducible (§6.3)
        return SoftwareRenderSurface::create(metrics);
    }

    void SoftwareRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!surface.valid())
            return;
        // metrics with no area cannot be rasterized, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // the extent is written here, on the thread that owns the surface (§5.3, D9.1)
        surface.resize(metrics);
    }

    void SoftwareRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // a frame needs a surface that could draw (§6.1)
        if (!surface.valid())
            return;
        // a scene at stale metrics is refused rather than repaired here, exactly as the gpu backend does (§6.1, D9.1)
        if (surface.metrics() != scene.metrics())
            return;
        // stage two begins the frame and draws the immutable scene into it (§6.1, AD-18)
        RenderContext& context = surface.begin_frame();
        // the background is a full surface rectangle, which is the clear of phase 1 (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
        context.draw_rect(full, {.fill = scene.background()});
        // the commands replay through the one shared replay, which is what the gpu backend also calls (§6.1, §6.2)
        replay_commands(scene.commands(), context, full);
        // the frame finishes once the immutable scene is fully recorded (§6.1)
        surface.end_frame();
    }

    void install_software_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<SoftwareRendererBackend>(); });
    }
} // namespace albor::software
