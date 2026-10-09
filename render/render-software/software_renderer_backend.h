#pragma once

#include <memory>

#include "render/renderer_backend.h"

namespace albor::software
{
    // the renderer backend that rasterizes on the cpu, which is what ci compares a gpu frame against (§6.3, AD-26)
    class SoftwareRendererBackend final : public RendererBackend
    {
    public:
        SoftwareRendererBackend();
        ~SoftwareRendererBackend() override;

        SoftwareRendererBackend(const SoftwareRendererBackend&) = delete;
        SoftwareRendererBackend& operator=(const SoftwareRendererBackend&) = delete;
        SoftwareRendererBackend(SoftwareRendererBackend&&) = delete;
        SoftwareRendererBackend& operator=(SoftwareRendererBackend&&) = delete;

        // override: the cpu rasterizer is always ready, because it needs no device and no layer (§6.3)
        [[nodiscard]] bool ready() const override;

        // override: allocate a raster surface of the given metrics, ignoring the native handle (§6.2, AD-26)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) override;

        // override: reallocate the raster extent, on the thread that owns the surface (§6.2, D9.1)
        void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) override;

        // override: replay the immutable scene onto the cpu raster (§6.1, AD-18)
        void execute_frame(RenderSurface& surface, const FrameScene& scene) override;
    };

    // install the cpu backend as the composition root's backend, which is what a headless run does (§6.3, AD-26)
    void install_software_renderer_backend();
} // namespace albor::software
