#pragma once

#include <memory>

#include "core/native_window_handle.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::software
{
    // the cpu raster and the context a frame draws into, so that no skia header leaves this file (§3.5)
    struct SoftwareSurfaceState;

    // the surface that rasterizes on the cpu, which is the reference CI compares against (§6.3, AD-26)
    class SoftwareRenderSurface final : public RenderSurface
    {
    public:
        // allocate a raster surface of the given metrics, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<SoftwareRenderSurface> create(WindowMetrics metrics);

        ~SoftwareRenderSurface() override;

        SoftwareRenderSurface(const SoftwareRenderSurface&) = delete;
        SoftwareRenderSurface& operator=(const SoftwareRenderSurface&) = delete;
        SoftwareRenderSurface(SoftwareRenderSurface&&) = delete;
        SoftwareRenderSurface& operator=(SoftwareRenderSurface&&) = delete;

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
        [[nodiscard]] std::vector<std::uint8_t> read_pixels() const;

        // report how many frames this surface finished, which is what a headless test counts (§4.3)
        [[nodiscard]] std::size_t frame_count() const;

    private:
        // allocate the raster of the given metrics, which a create and a resize both do
        explicit SoftwareRenderSurface(WindowMetrics metrics);

        // the raster and the context of the frame being drawn, which are behind one pimpl so no skia header leaks (§3.5)
        std::unique_ptr<SoftwareSurfaceState> m_state;
    };
} // namespace albor::software
