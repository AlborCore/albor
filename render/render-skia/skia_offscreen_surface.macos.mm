#include "render/render-skia/skia_offscreen_surface.h"

#include <algorithm>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/GpuTypes.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>

#include "render/render-skia/skia_render_context.h"

namespace albor::skia
{
    // the skia render target and the context of the frame being drawn, so that no skia type leaves the header (§3.5)
    struct SkiaOffscreenState
    {
        // the direct context the target records into, which the backend owns
        GrDirectContext* context = nullptr;

        // the render target the frame is drawn into, which persists so it can be read back (§6.3)
        sk_sp<SkSurface> surface;

        // the context that draws the current frame, which owns the canvas of the target
        std::unique_ptr<SkiaRenderContext> draw_context;

        // the metrics the target renders at
        WindowMetrics metrics;
    };

    namespace
    {
        // the image info a target allocates at, which is the format the read back compares (§6.3, AD-26)
        SkImageInfo target_info(const WindowMetrics& metrics) {
            // a target always has at least one pixel, because skia refuses a zero extent (§6.3)
            const int width = static_cast<int>(std::max(1.0f, metrics.pixel_size.width));
            // the height follows the physical extent for the same reason (§6.3)
            const int height = static_cast<int>(std::max(1.0f, metrics.pixel_size.height));
            // premultiplied rgba is the format every backend reads back as (§6.3, AD-26)
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        }

        // build a render target over the recording context, returning null when skia refuses it (§6.3)
        sk_sp<SkSurface> make_target(GrDirectContext* context, const WindowMetrics& metrics) {
            // the target is unbudgeted, because a parity check keeps it alive for the whole comparison (§6.3)
            return SkSurfaces::RenderTarget(context, skgpu::Budgeted::kNo, target_info(metrics));
        }
    } // namespace

    std::unique_ptr<SkiaOffscreenSurface> SkiaOffscreenSurface::create(void* recording_context, WindowMetrics metrics) {
        // a target without a recording context has nothing to record into (§6.3)
        if (recording_context == nullptr)
            return nullptr;
        // a target with no area cannot be read back, so the metrics are refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        // the state carries the context and the target, which the surface owns (§6.3)
        auto state = std::make_unique<SkiaOffscreenState>();
        state->context = static_cast<GrDirectContext*>(recording_context);
        state->metrics = metrics;
        // the target is the gpu memory the frame is drawn into, which is what makes parity headless (§6.3, AD-26)
        state->surface = make_target(state->context, metrics);
        // a target skia refused leaves no surface to draw into (§6.3)
        if (state->surface == nullptr)
            return nullptr;
        return std::unique_ptr<SkiaOffscreenSurface>(new SkiaOffscreenSurface(std::move(state)));
    }

    SkiaOffscreenSurface::~SkiaOffscreenSurface() {
        // the target is dropped before the context that backs it (§6.3)
        m_state->surface = nullptr;
    }

    bool SkiaOffscreenSurface::valid() const {
        // the target is usable exactly when it allocated its gpu memory (§6.3)
        return m_state->surface != nullptr;
    }

    const WindowMetrics& SkiaOffscreenSurface::metrics() const {
        // the metrics are what the target was built or last resized to (§6.2)
        return m_state->metrics;
    }

    void SkiaOffscreenSurface::resize(const WindowMetrics& metrics) {
        // metrics with no area cannot be read back, so the previous target is kept (§6.2)
        if (!metrics.valid())
            return;
        // a context that drew into the old target cannot draw into the new one, so it is dropped (§6.3)
        m_state->draw_context = nullptr;
        // the target is reallocated at the new metrics, which is what a render target offers (§6.3)
        m_state->surface = make_target(m_state->context, metrics);
        // the metrics move with the target, because a stale extent is not a frame (§6.1)
        m_state->metrics = metrics;
    }

    RenderContext& SkiaOffscreenSurface::begin_frame() {
        // a target that did not allocate hands a null canvas, which the context absorbs as a no op (§6.3)
        SkCanvas* canvas = m_state->surface != nullptr ? m_state->surface->getCanvas() : nullptr;
        // the context draws the logical scene scaled onto the physical target (§6.3, AD-27)
        m_state->draw_context = std::make_unique<SkiaRenderContext>(canvas, m_state->metrics.scale_factor);
        return *m_state->draw_context;
    }

    void SkiaOffscreenSurface::end_frame() {
        // a target that did not allocate has nothing to submit (§6.1)
        if (m_state->surface == nullptr || m_state->context == nullptr)
            return;
        // the recorded work is submitted so the gpu target holds the finished frame when it is read (§6.1, §6.3)
        m_state->context->flushAndSubmit(m_state->surface.get());
        // the draw context is dropped, so the next begin_frame builds a fresh one over the same target (§6.3)
        m_state->draw_context = nullptr;
    }

    std::vector<std::uint8_t> SkiaOffscreenSurface::read_pixels() const {
        // a target that did not allocate has no pixels, which is a refusal (§6.3)
        if (m_state->surface == nullptr)
            return {};
        // the read uses the canonical info, because two backends must produce bytes a comparison can read (§6.3, AD-26)
        const SkImageInfo info = SkImageInfo::Make(m_state->surface->width(), m_state->surface->height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        // the stride is the padded row, which skia rounds per row rather than reporting per image (§6.3)
        const std::size_t stride = info.minRowBytes();
        // the buffer is sized from the minimum byte size, because a row is padded to its own stride (§6.3)
        std::vector<std::uint8_t> pixels(info.computeMinByteSize());
        // an extent the info cannot size is refused, because a wrapped size is not a frame (§6.3)
        if (SkImageInfo::ByteSizeOverflowed(pixels.size()))
            return {};
        // the read is refused when the target cannot be copied, because a half read is not a screenshot (§6.3)
        if (!m_state->surface->readPixels(info, pixels.data(), stride, 0, 0))
            return {};
        // the tightly packed bytes are what a conformance check compares (§6.3, AD-26)
        return pixels;
    }

    SkiaOffscreenSurface::SkiaOffscreenSurface(std::unique_ptr<SkiaOffscreenState> state) :
        m_state(std::move(state)) {}
} // namespace albor::skia
