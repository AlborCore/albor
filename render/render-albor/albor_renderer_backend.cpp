#include "render/render-albor/albor_renderer_backend.h"

#include "render/draw_command.h"
#include "render/render-albor/albor_backend_support.h"
#include "render/renderer_backend.h"

namespace albor::render
{
    AlborRendererBackend::AlborRendererBackend() = default;

    AlborRendererBackend::~AlborRendererBackend() = default;

    bool AlborRendererBackend::ready() const {
        // the platform device decides whether a frame can be drawn at all (§6.2)
        return backend_device_available();
    }

    std::unique_ptr<RenderSurface> AlborRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a surface is refused rather than half built while the device is not ready (§6.2)
        if (!ready())
            return nullptr;
        // metrics with no area cannot be presented, so the surface is refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        // the platform builds the surface over the handle, which a null handle leaves offscreen (§6.2)
        return backend_create_surface(handle, metrics);
    }

    void AlborRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!surface.valid())
            return;
        // metrics with no area cannot be presented, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // the extent is written here, on the thread that owns the surface (§5.3, D9.1)
        surface.resize(metrics);
    }

    void AlborRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // a frame needs a surface that could draw (§6.1)
        if (!surface.valid())
            return;
        // a scene at stale metrics is refused rather than repaired here, exactly as the reference does (§6.1, D9.1)
        if (surface.metrics() != scene.metrics())
            return;
        // stage two begins the frame and draws the immutable scene into it (§6.1, AD-18)
        RenderContext& context = surface.begin_frame();
        // the background is a full surface rectangle, which the clear of phase one is (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
        context.draw_rect(full, {.fill = scene.background()});
        // the commands replay through the one shared replay, which every backend calls (§6.1, §6.2)
        replay_commands(scene.commands(), context, full);
        // the frame finishes once the immutable scene is fully recorded (§6.1)
        surface.end_frame();
    }

    void install_albor_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<AlborRendererBackend>(); });
    }
} // namespace albor::render
