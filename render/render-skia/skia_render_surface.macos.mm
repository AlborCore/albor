#include "render/render-skia/skia_render_surface.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkColorType.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/mtl/SkSurfaceMetal.h>

#include "render/render-skia/skia_render_context.h"

namespace albor::skia
{
    // the metal layer and the skia surface of the frame being drawn, so that no platform type leaves this header (§3.5)
    struct SkiaSurfaceState
    {
        // the layer the window attached, which holds the device and the drawable pool
        CAMetalLayer* layer = nil;

        // the skia direct context the surface records into, which the backend owns
        GrDirectContext* context = nullptr;

        // the surface of the frame being drawn, which acquires a drawable when it is wrapped
        sk_sp<SkSurface> frame;

        // the drawable the current frame acquired, which the client presents after the submit (§6.1)
        GrMTLHandle drawable = nullptr;

        // the context that draws the current frame, which owns the canvas of the wrapped surface
        std::unique_ptr<SkiaRenderContext> draw_context;

        // the metrics the surface presents at
        WindowMetrics metrics;

        // the image of the last presented frame, which is the only way to read a gpu frame back (§6.3, AD-26)
        sk_sp<SkImage> last_frame;

        // whether the last wrap produced a surface that could be drawn into
        bool ready = false;
    };

    namespace
    {
        void apply_metrics(CAMetalLayer* layer, const WindowMetrics& metrics) {
            // a layer with no device cannot size a drawable, so the resize is refused (§6.3)
            if (layer == nil || layer.device == nil)
                return;
            // the layer size is in points, while the drawable size is in physical pixels (AD-27)
            layer.frame = CGRectMake(0.0, 0.0, metrics.logical_size.width, metrics.logical_size.height);
            layer.contentsScale = metrics.scale_factor;
            layer.drawableSize = CGSizeMake(metrics.pixel_size.width, metrics.pixel_size.height);
        }
    } // namespace

    std::unique_ptr<SkiaRenderSurface> SkiaRenderSurface::create(void* recording_context, NativeWindowHandle handle, WindowMetrics metrics) {
        // a surface without a recording context has nothing to record into (§6.3)
        if (recording_context == nullptr)
            return nullptr;
        // the handle must carry the metal layer the window attached (§3.5, AD-16)
        if (!handle.valid())
            return nullptr;
        // a surface with no area cannot be presented, so the handle is refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        std::unique_ptr<SkiaSurfaceState> state = std::make_unique<SkiaSurfaceState>();
        state->context = static_cast<GrDirectContext*>(recording_context);
        state->metrics = metrics;
        // the opaque handle carries the metal layer, which only this translation unit may name (§3.5, AD-05)
        state->layer = (__bridge CAMetalLayer*)const_cast<void*>(handle.object());
        // a layer that never got a device cannot produce drawables, so the surface is refused (§6.3)
        if (state->layer == nil || state->layer.device == nil)
            return nullptr;
        // the first resize is what sizes the drawable pool of the layer (§6.2)
        apply_metrics(state->layer, metrics);
        return std::unique_ptr<SkiaRenderSurface>(new SkiaRenderSurface(std::move(state)));
    }

    SkiaRenderSurface::~SkiaRenderSurface() {
        // the frame holds a drawable that belongs to the layer, so it is released before the surface goes away (§6.3)
        m_state->drawable = nullptr;
        m_state->frame = nullptr;
    }

    bool SkiaRenderSurface::valid() const {
        // the surface is presentable exactly while its layer still carries a device (§6.3)
        return m_state->layer != nil && m_state->layer.device != nil;
    }

    const WindowMetrics& SkiaRenderSurface::metrics() const {
        // the metrics are what the last resize established (§6.2)
        return m_state->metrics;
    }

    void SkiaRenderSurface::resize(const WindowMetrics& metrics) {
        // metrics with no area cannot be presented, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        m_state->metrics = metrics;
        apply_metrics(m_state->layer, metrics);
    }

    RenderContext& SkiaRenderSurface::begin_frame() {
        // the drawable is released last frame, so the wrap hands the this frame's drawable to this slot (§6.3)
        m_state->drawable = nullptr;
        // wrapping the layer is what acquires a drawable, so a frame is one wrap and one draw (§6.1)
        m_state->frame = SkSurfaces::WrapCAMetalLayer(m_state->context, (__bridge const void*)m_state->layer, GrSurfaceOrigin::kTopLeft_GrSurfaceOrigin, 1, SkColorType::kBGRA_8888_SkColorType, nullptr, nullptr, &m_state->drawable);
        // a layer that cannot hand out a drawable drops the frame rather than drawing into nothing (§6.1)
        if (m_state->frame == nullptr) {
            m_state->ready = false;
            m_state->draw_context = std::make_unique<SkiaRenderContext>(nullptr, m_state->metrics.scale_factor);
            return *m_state->draw_context;
        }
        m_state->ready = true;
        // the context draws the logical scene scaled onto the physical drawable (§6.3, AD-27)
        m_state->draw_context = std::make_unique<SkiaRenderContext>(m_state->frame->getCanvas(), m_state->metrics.scale_factor);
        return *m_state->draw_context;
    }

    void SkiaRenderSurface::end_frame() {
        // a frame that never began has nothing to submit (§6.1)
        if (!m_state->ready || m_state->frame == nullptr || m_state->context == nullptr)
            return;
        // the presented frame is snapshotted before it is released, because a drawable returns to the pool (§6.3)
        m_state->last_frame = m_state->frame->makeImageSnapshot();
        // the grant resolves the drawable through the slot captured by the wrap, so the presentation sees it (§6.3)
        m_state->context->flushAndSubmit(m_state->frame.get());
        // the drawable is presented last, because the compositor may only show a submitted frame (§6.1)
        if (m_state->drawable != nullptr) {
            // the drawable crosses back into objective c only here, which is the sole presentation point (§6.1)
            id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)m_state->drawable;
            [drawable present];
            m_state->drawable = nullptr;
        }
        // the surface is released so that its drawable returns to the layer pool for the next frame (§6.1)
        m_state->frame = nullptr;
        m_state->draw_context = nullptr;
        m_state->ready = false;
    }

    std::vector<std::uint8_t> SkiaRenderSurface::read_pixels() const {
        // a surface that never presented has nothing to read, which is a refusal rather than an error (§6.3)
        if (m_state->last_frame == nullptr)
            return {};
        // the read uses the canonical info, because two backends must produce bytes a comparison can read (§6.3, AD-26)
        const SkImageInfo info = SkImageInfo::Make(m_state->last_frame->width(), m_state->last_frame->height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        // the stride is the padded row, which skia rounds per row rather than reporting per image (§6.3)
        const std::size_t stride = info.minRowBytes();
        // the buffer is sized from the minimum byte size, because a row is padded to its own stride (§6.3)
        std::vector<std::uint8_t> pixels(info.computeMinByteSize());
        // an extent the info cannot size is refused, because a wrapped size is not a frame (§6.3)
        if (SkImageInfo::ByteSizeOverflowed(pixels.size()))
            return {};
        // the read is refused when the image cannot be copied, because a half read is not a screenshot (§6.3)
        if (!m_state->last_frame->readPixels(info, pixels.data(), stride, 0, 0))
            return {};
        // the tightly packed bytes are what a conformance check compares (§6.3, AD-26)
        return pixels;
    }

    SkiaRenderSurface::SkiaRenderSurface(std::unique_ptr<SkiaSurfaceState> state) :
        m_state(std::move(state)) {}
} // namespace albor::skia
