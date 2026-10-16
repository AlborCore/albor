#pragma once

#include <memory>

#include "render/renderer_backend.h"

namespace albor::blend2d
{
    // the renderer backend that rasterizes on the cpu through blend2d, a portable alternative to skia (§6.3, AD-26)
    class Blend2DRendererBackend final : public RendererBackend
    {
    public:
        Blend2DRendererBackend();
        ~Blend2DRendererBackend() override;

        Blend2DRendererBackend(const Blend2DRendererBackend&) = delete;
        Blend2DRendererBackend& operator=(const Blend2DRendererBackend&) = delete;
        Blend2DRendererBackend(Blend2DRendererBackend&&) = delete;
        Blend2DRendererBackend& operator=(Blend2DRendererBackend&&) = delete;

        // override: the blend2d rasterizer needs no device and no layer, so it is always ready (§6.3)
        [[nodiscard]] bool ready() const override;

        // override: allocate a raster surface of the given metrics, ignoring the native handle (§6.2, AD-26)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) override;

        // override: reallocate the raster extent, on the thread that owns the surface (§6.2, D9.1)
        void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) override;

        // override: replay the immutable scene onto the cpu raster (§6.1, AD-18)
        void execute_frame(RenderSurface& surface, const FrameScene& scene) override;
    };

    // install the blend2d backend as the composition root's backend, which a headless run does (§6.3, AD-26)
    void install_blend2d_renderer_backend();
} // namespace albor::blend2d
