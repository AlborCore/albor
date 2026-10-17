#pragma once

#include <memory>

#include "core/native_window_handle.h"
#include "render/renderer_backend.h"
#include "window/window_metrics.h"

namespace albor
{
    // the opaque skia and metal state, defined only in the objective c++ translation unit (§3.5, AD-05)
    struct SkiaRendererBackendState;

    // the skia backend that presents through a macOS CAMetalLayer on a metal device (§6.3, AD-05)
    class SkiaRendererBackend final : public RendererBackend
    {
    public:
        // bring up the metal device and the skia direct context, which reports failure through ready()
        SkiaRendererBackend();

        // release the skia context before the metal device it borrowed from goes away
        ~SkiaRendererBackend() override;

        SkiaRendererBackend(const SkiaRendererBackend&) = delete;
        SkiaRendererBackend& operator=(const SkiaRendererBackend&) = delete;
        SkiaRendererBackend(SkiaRendererBackend&&) = delete;
        SkiaRendererBackend& operator=(SkiaRendererBackend&&) = delete;

        // override: report whether the graphics device came up
        [[nodiscard]] bool ready() const override;

        // override: wrap a metal layer into a presentable surface
        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) override;

        // build an offscreen gpu target, which a parity check draws into without a window (§6.3, AD-26)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_offscreen_surface(const WindowMetrics& metrics);

        // override: resize a surface, which a window resize requires from the ui thread (§5.3, D9.1)
        void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) override;

        // override: execute a frame scene, refusing one at stale metrics rather than repairing it (§6.1, D9.1)
        void execute_frame(RenderSurface& surface, const FrameScene& scene) override;

    private:
        // the opaque skia state, so that no skia type reaches this header (§3.5, AD-05)
        std::unique_ptr<SkiaRendererBackendState> m_state;
    };

    // install the skia backend as the renderer the framework draws with (§6.3)
    void install_skia_renderer_backend();
} // namespace albor
