#pragma once

#include <memory>

#include "core/native_window_handle.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::skia
{
    // the metal layer and the skia context a surface presents through, so that no platform type leaves this header (§3.5)
    struct SkiaSurfaceState;

    // the render surface that wraps a macOS CAMetalLayer and presents skia frames into it (§6.3, AD-16)
    class SkiaRenderSurface final : public RenderSurface
    {
    public:
        // wrap the metal layer of a window, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<SkiaRenderSurface> create(void* recording_context, NativeWindowHandle handle, WindowMetrics metrics);

        // release the skia surface of the last frame, which returns its drawable to the layer
        ~SkiaRenderSurface() override;

        SkiaRenderSurface(const SkiaRenderSurface&) = delete;
        SkiaRenderSurface& operator=(const SkiaRenderSurface&) = delete;
        SkiaRenderSurface(SkiaRenderSurface&&) = delete;
        SkiaRenderSurface& operator=(SkiaRenderSurface&&) = delete;

        // override: report whether the layer produced a presentable surface
        [[nodiscard]] bool valid() const override;

        // override: hand out the metrics the surface presents at
        [[nodiscard]] const WindowMetrics& metrics() const override;

        // override: resize the layer, which the thread that created the native window owns (§5.3, D9.1)
        void resize(const WindowMetrics& metrics) override;

        // override: acquire a drawable and hand out the context that draws into it
        RenderContext& begin_frame() override;

        // override: flush the work to the gpu and present the drawable
        void end_frame() override;

    private:
        // build the surface over its opaque state
        explicit SkiaRenderSurface(std::unique_ptr<SkiaSurfaceState> state);

        // the opaque metal and skia state, so that no platform type leaves this header (§3.5, AD-05)
        std::unique_ptr<SkiaSurfaceState> m_state;
    };
} // namespace albor::skia
