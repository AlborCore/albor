#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::skia
{
    // the skia render target state, so that no platform or skia type leaves this header (§3.5, AD-05)
    struct SkiaOffscreenState;

    // an offscreen gpu render target, which a parity check draws into without a window (§6.3, AD-26)
    class SkiaOffscreenSurface final : public RenderSurface
    {
    public:
        // build a render target over the backend's recording context, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<SkiaOffscreenSurface> create(void* recording_context, WindowMetrics metrics);

        // release the render target, which flushes any work the gpu still holds
        ~SkiaOffscreenSurface() override;

        SkiaOffscreenSurface(const SkiaOffscreenSurface&) = delete;
        SkiaOffscreenSurface& operator=(const SkiaOffscreenSurface&) = delete;
        SkiaOffscreenSurface(SkiaOffscreenSurface&&) = delete;
        SkiaOffscreenSurface& operator=(SkiaOffscreenSurface&&) = delete;

        // override: report whether the render target came up
        [[nodiscard]] bool valid() const override;

        // override: hand out the metrics the target renders at
        [[nodiscard]] const WindowMetrics& metrics() const override;

        // override: reallocate the target at the new metrics, which a parity check may do between scenes
        void resize(const WindowMetrics& metrics) override;

        // override: hand out the context that draws into the target
        RenderContext& begin_frame() override;

        // override: submit the recorded work to the gpu, which is what makes the target readable
        void end_frame() override;

        // override: read the rendered frame back, which is what a parity check compares (§6.3, AD-26)
        [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override;

    private:
        // build the surface over its opaque state
        explicit SkiaOffscreenSurface(std::unique_ptr<SkiaOffscreenState> state);

        // the opaque skia state, so that no skia type leaves this header (§3.5, AD-05)
        std::unique_ptr<SkiaOffscreenState> m_state;
    };
} // namespace albor::skia
