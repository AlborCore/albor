#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::blend2d
{
    // the raster and the context a frame draws into, so that no blend2d header leaves this file (§3.5)
    struct Blend2DSurfaceState;

    // the surface that rasterizes on the cpu through blend2d, a portable alternative to the skia reference (§6.3, AD-26)
    class Blend2DRenderSurface final : public RenderSurface
    {
    public:
        // allocate a raster surface of the given metrics, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<Blend2DRenderSurface> create(WindowMetrics metrics);

        ~Blend2DRenderSurface() override;

        Blend2DRenderSurface(const Blend2DRenderSurface&) = delete;
        Blend2DRenderSurface& operator=(const Blend2DRenderSurface&) = delete;
        Blend2DRenderSurface(Blend2DRenderSurface&&) = delete;
        Blend2DRenderSurface& operator=(Blend2DRenderSurface&&) = delete;

        // override: report whether the raster allocation produced a drawable
        [[nodiscard]] bool valid() const override;

        // override: hand out the metrics the surface rasterizes at
        [[nodiscard]] const WindowMetrics& metrics() const override;

        // override: replace the raster extent, which a window resize requires (§6.2)
        void resize(const WindowMetrics& metrics) override;

        // override: begin the frame and hand out the context that draws it (§6.1)
        RenderContext& begin_frame() override;

        // override: end the frame the begin kept, exactly once per begun frame (§6.1)
        void end_frame() override;

        // read the pixels as packed bytes, which is the screenshot a conformance check compares (§6.3, AD-26)
        [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override;

        // copy the finished frame in native premultiplied bgra rows, which a present path hands to its display (§6.3)
        [[nodiscard]] bool copy_native_pixels(void* destination, std::size_t row_bytes) const;

        // report how many frames this surface finished, which is what a headless test counts (§4.3)
        [[nodiscard]] std::size_t frame_count() const;

    private:
        // allocate the raster of the given metrics, which a create and a resize both do
        explicit Blend2DRenderSurface(WindowMetrics metrics);

        // the raster and the context of the frame being drawn, behind one pimpl so no blend2d header leaks (§3.5)
        std::unique_ptr<Blend2DSurfaceState> m_state;
    };
} // namespace albor::blend2d
