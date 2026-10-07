#include "render/render-skia/skia_renderer_backend.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlBackendContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlDirectContext.h>

#include "render/render-skia/skia_render_surface.h"

namespace albor
{
    // the metal objects and the skia direct context the backend owns, so that no platform type leaks (§3.5)
    struct SkiaRendererBackendState
    {
        // the metal device the frames are encoded for
        id<MTLDevice> device = nil;

        // the metal command queue the frames are submitted to
        id<MTLCommandQueue> queue = nil;

        // the skia direct context, which is null when the backend refused the device
        sk_sp<GrDirectContext> context;
    };

    namespace
    {
        std::unique_ptr<SkiaRendererBackendState> build_backend_state() {
            // a host without a metal device cannot present through skia at all (§6.3)
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil)
                return nullptr;
            // the state owns the device and the queue for as long as the backend records gpu work (§6.3)
            std::unique_ptr<SkiaRendererBackendState> state = std::make_unique<SkiaRendererBackendState>();
            state->device = device;
            state->queue = [device newCommandQueue];
            // a device without a command queue has nowhere to submit frames, so the backend refuses it (§6.3)
            if (state->queue == nil)
                return nullptr;
            // skia borrows the device and the queue through bridged handles, so it does not retain them itself (§6.3)
            GrMtlBackendContext backend;
            backend.fDevice = sk_cfp<GrMTLHandle>((__bridge const void*)device);
            backend.fQueue = sk_cfp<GrMTLHandle>((__bridge const void*)state->queue);
            // the direct context is what turns recorded draws into gpu commands (§6.1, §6.3)
            state->context = GrDirectContexts::MakeMetal(backend);
            // a context that did not come up leaves the backend unusable rather than half working (§6.3)
            if (state->context == nullptr)
                return nullptr;
            return state;
        }
    } // namespace

    SkiaRendererBackend::SkiaRendererBackend() :
        m_state(build_backend_state()) {}

    SkiaRendererBackend::~SkiaRendererBackend() {
        // the context holds gpu work that references the device, so it is abandoned before the state is dropped (§6.3)
        if (m_state && m_state->context)
            m_state->context->abandonContext();
    }

    bool SkiaRendererBackend::ready() const {
        // the backend is ready exactly when its state carries a direct context (§6.3)
        return m_state != nullptr;
    }

    std::unique_ptr<RenderSurface> SkiaRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // without a context there is no way to record gpu work (§6.3)
        if (!ready())
            return nullptr;
        // the handle must carry the metal layer the window attached (§3.5, AD-16)
        if (!handle.valid())
            return nullptr;
        // a surface with no area cannot be presented, so the handle is refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        // the opaque handle carries the metal layer, which only this translation unit may name (§3.5, AD-05)
        CAMetalLayer* layer = (__bridge CAMetalLayer*)const_cast<void*>(handle.object());
        // the layer needs the device before it can hand out drawables (§6.3)
        layer.device = m_state->device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        // the surface wraps the skia context and the metal layer together, because one drawable spans both (§6.3)
        return skia::SkiaRenderSurface::create(m_state->context.get(), handle, metrics);
    }

    void SkiaRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!surface.valid())
            return;
        // metrics with no area cannot be presented, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        surface.resize(metrics);
    }

    void SkiaRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // a frame needs a surface that could acquire a drawable (§6.1)
        if (!surface.valid())
            return;
        // the scene is built at metrics that no longer match the surface, so the surface is resized first (§6.1)
        if (surface.metrics() != scene.metrics())
            resize_surface(surface, scene.metrics());
        // stage two acquires the drawable and draws the immutable scene into it (§6.1, AD-18)
        RenderContext& context = surface.begin_frame();
        // the background is a full surface rectangle, which is the clear of phase 1 (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
        context.draw_rect(full, {.fill = scene.background()});
        // the solid rectangles of the scene are drawn in the order the ui thread built them (§6.1)
        for (std::size_t index = 0; index < scene.rects().size(); index++) {
            const Rect& rect = scene.rects()[index];
            // a rectangle with no area contributes nothing, so it is skipped rather than drawn degenerate (§6.2)
            if (rect.empty())
                continue;
            // the colour stage one recorded is used, because a ui node may bind its own colour (§6.1, AD-18)
            context.draw_rect(rect, {.fill = index < scene.rect_colors().size() ? scene.rect_colors()[index] : Color::rgb(0.20f, 0.55f, 0.90f)});
        }
        // the text runs of the scene are drawn at the origins the ui thread recorded, in the colours it recorded, which keeps stage two free of ui state (§6.1, AD-18)
        for (std::size_t index = 0; index < scene.texts().size(); index++)
            context.draw_text(scene.texts()[index], scene.text_origins()[index], {.fill = index < scene.text_colors().size() ? scene.text_colors()[index] : Color::rgb(0.90f, 0.92f, 0.95f)});
        // the retained line batches replay in the order the ui thread recorded them, with the transform stage one composed (§6.4, AD-06)
        for (const LineBatchDraw& draw : scene.line_batches())
            context.draw_line_batch(draw.batch, draw.transform, draw.stroke);
        // the retained paths replay in the order the ui thread recorded them (§6.4, AD-06)
        for (const PathDraw& draw : scene.paths())
            context.draw_path(draw.path, draw.transform, draw.paint);
        // the retained pictures replay in the order the ui thread recorded them (§6.4, AD-06)
        for (const PictureDraw& draw : scene.pictures())
            context.draw_picture(draw.picture, draw.transform);
        // the retained images replay in the order the ui thread recorded them, which uploads each upload once (§6.4, AD-06)
        for (const ImageDraw& draw : scene.images())
            context.draw_image(draw.image, draw.destination, Paint{});
        // the frame is presented once the immutable scene is fully recorded (§6.1)
        surface.end_frame();
    }

    void install_skia_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<SkiaRendererBackend>(); });
    }
} // namespace albor
