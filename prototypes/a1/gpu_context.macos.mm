// the gpu context of one window: it owns the skia renderer, wraps the platform surface and submits frames (§9.2)
// milestone 148 as shipped by vcpkg has no graphite symbols and no default font manager, so this uses ganesh on
// metal and resolves fonts through core text; both findings are recorded in RESULTS.md

#include "prototypes/a1/gpu_context.h"

#import "prototypes/a2/native_surface.macos.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <chrono>
#include <thread>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlBackendContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlDirectContext.h>
#include <skia/gpu/ganesh/mtl/SkSurfaceMetal.h>

#include "prototypes/a1/custom_render_node.h"
#include "prototypes/a1/resource_set.h"

namespace albor::a1
{

    namespace
    {

        // the clock the gpu path measures with, monotonic like every other slice (§7.1)
        using Clock = std::chrono::steady_clock;

        // read the clock as milliseconds since the steady clock epoch, because taking the difference of two
        // time points is measurably unreliable here: it reported a constant 29 hour offset (see RESULTS.md)
        double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

    } // namespace

    struct GpuContext::State
    {
        // the platform surface the frames are presented into
        albor::a2::NativeSurface* surface = nullptr;

        // the ganesh context and the surface the current frame draws into
        sk_sp<GrDirectContext> context;
        sk_sp<SkSurface> frame;

        // what the current frame has cost so far, which the caller reports (§28.2)
        double last_submit = 0.0;
        bool has_last_submit = false;
        std::size_t frames_submitted = 0;
        std::uint64_t frame_index = 0;
        GpuFrameTiming timing{};

        // the injected presentation hold, which is zero unless a workload asked for one
        double hold_ms = 0.0;
    };

    GpuContext::GpuContext() :
        m_state(new State()) {}

    GpuContext::~GpuContext() {
        // release the renderer state
        delete m_state;
        m_state = nullptr;
    }

    bool GpuContext::create(albor::a2::PlatformWindow& window) {
        // resolve the native surface, which owns the metal device and queue (§9.2)
        m_state->surface = window.native_surface();
        if (m_state->surface == nullptr || m_state->surface->layer == nil || m_state->surface->queue == nil) {
            return false;
        }

        // hand skia the device and the queue; the handles are bridged, so this file does not retain them
        GrMtlBackendContext backend;
        backend.fDevice = sk_cfp<GrMTLHandle>(static_cast<const void*>(m_state->surface->device));
        backend.fQueue = sk_cfp<GrMTLHandle>(static_cast<const void*>(m_state->surface->queue));

        // bring up the ganesh context, which is the backend milestone 148 actually ships
        m_state->context = GrDirectContexts::MakeMetal(backend);
        return m_state->context != nullptr;
    }

    bool GpuContext::begin_frame() {
        // a frame needs a context, so a failed create cannot draw
        if (m_state->context == nullptr || m_state->surface == nullptr) {
            return false;
        }

        // wrapping the layer is what acquires a drawable, so this call is the acquire cost of §28.2
        const double wrap_start = now_ms();
        GrMTLHandle drawable = nullptr;
        sk_sp<SkSurface> surface = SkSurfaces::WrapCAMetalLayer(m_state->context.get(), static_cast<const void*>(m_state->surface->layer), GrSurfaceOrigin::kTopLeft_GrSurfaceOrigin, 1, SkColorType::kBGRA_8888_SkColorType, nullptr, nullptr, &drawable);
        const double acquire_ms = now_ms() - wrap_start;

        // a layer that cannot produce a surface is a dropped frame, and the caller has to see it
        // the drawable out param stays null in milestone 148 even on success, so it is not a failure signal
        if (surface == nullptr) {
            return false;
        }

        // the surface holds the drawable it wrapped, so the handle itself is not needed here
        static_cast<void>(drawable);

        // keep the surface for the draw and the submit
        m_state->frame = std::move(surface);

        // reset the frame's measurements
        m_state->timing = GpuFrameTiming{};
        m_state->timing.index = m_state->frame_index;
        m_state->timing.acquire_ms = acquire_ms;
        return true;
    }

    void GpuContext::draw(CustomRenderNode& node) {
        // nothing to draw into means the frame was dropped upstream
        if (m_state->frame == nullptr) {
            return;
        }

        // the retained set is immutable, so reading it here runs no application code (§10.2)
        const ResourceSetPtr& resources = node.resources();
        if (resources == nullptr) {
            return;
        }

        // start the recording clock, which is what the record cost is measured against
        const double record_start = now_ms();
        SkCanvas* canvas = m_state->frame->getCanvas();

        // clear the frame, because the retained geometry is additive over a dark background
        canvas->clear(SK_ColorBLACK);

        // apply the view transform to the canvas, which is the o(1) part of a pan or a zoom
        canvas->save();
        canvas->translate(node.view().offset_x, node.view().offset_y);
        canvas->scale(node.view().scale, node.view().scale);

        // replay the retained geometry, one draw call per retained batch, which is what §11.2 promises
        for (const sk_sp<SkVertices>& batch : resources->line_batches) {
            canvas->drawVertices(batch, SkBlendMode::kSrcOver, resources->line_paint);
        }

        // draw the labels from the single label buffer, which the cad workload needs
        if (resources->font.getTypeface() != nullptr && !resources->labels.empty()) {
            for (const Label& label : resources->labels) {
                // read this label's slice of the shared buffer
                const char* text = resources->label_buffer.data() + label.offset;
                canvas->drawSimpleText(text, label.length, SkTextEncoding::kUTF8, label.x, label.y, resources->font, resources->label_paint);
            }
        }

        // restore the canvas, so the next frame starts from the identity transform
        canvas->restore();
        m_state->timing.record_ms = now_ms() - record_start;
    }

    void GpuContext::end_frame() {
        // a frame that never began has nothing to submit
        if (m_state->frame == nullptr || m_state->context == nullptr) {
            return;
        }

        // start the submit clock, which is what the submit cost is measured against
        const double submit_start = now_ms();

        // flush and submit, which is what actually hands the work to the gpu
        m_state->context->flush(m_state->frame.get());
        m_state->context->submit();
        m_state->timing.submit_ms = now_ms() - submit_start;

        // apply the injected hold, which is how w3 models a gpu presentation stall
        if (m_state->hold_ms > 0.0) {
            const double hold_start = now_ms();
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(m_state->hold_ms));
            m_state->timing.hold_ms = now_ms() - hold_start;
        }

        // record the interval since the previous submit, which is the cadence this window reached
        const double now = now_ms();
        m_state->timing.interval_ms = m_state->has_last_submit ? now - m_state->last_submit : 0.0;
        m_state->last_submit = now;
        m_state->has_last_submit = true;
        m_state->frames_submitted++;
        m_state->frame_index++;

        // let the surface go before the next wrap, so the drawable returns to the pool
        m_state->frame = nullptr;
    }

    void GpuContext::set_presentation_hold_ms(double hold_ms) { m_state->hold_ms = hold_ms; }

    const char* GpuContext::backend() const {
        // report the backend that actually came up, because §11.7 compares backends (§11.7)
        return m_state->context == nullptr ? "none" : "ganesh-metal";
    }

    std::size_t GpuContext::frames_submitted() const { return m_state->frames_submitted; }

    const GpuFrameTiming& GpuContext::last_timing() const { return m_state->timing; }

} // namespace albor::a1
