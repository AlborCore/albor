#include "render/render-albor/albor_renderer_backend.h"

#include "render/renderer_backend.h"

namespace albor::render
{
    AlborRendererBackend::AlborRendererBackend() = default;

    AlborRendererBackend::~AlborRendererBackend() = default;

    bool AlborRendererBackend::ready() const {
        // the device arrives with the metal translation unit, so the shell is never ready (§6.2, AD-16)
        return false;
    }

    std::unique_ptr<RenderSurface> AlborRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // the shell attaches to no platform object, because it has no device that could present into one (§6.2)
        (void)handle;
        // the metrics name an extent the shell cannot allocate, because the device owns the presentation target (§6.2)
        (void)metrics;
        // a surface is refused rather than half built while the backend is not ready (§6.2)
        return nullptr;
    }

    void AlborRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // the shell owns no surface, so there is nothing to reallocate (§6.2)
        (void)surface;
        // the metrics are not reached for the same reason (§6.2)
        (void)metrics;
    }

    void AlborRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // the shell owns no surface and no device, so stage two has nothing to execute onto (§6.1, AD-18)
        (void)surface;
        // the scene is not replayed for the same reason (§6.1, AD-18)
        (void)scene;
    }

    void install_albor_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<AlborRendererBackend>(); });
    }
} // namespace albor::render
