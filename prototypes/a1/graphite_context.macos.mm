// the offscreen graphite context of a1.4: it brings up the metal backend, allocates a surface and submits
// frames without presenting, because the public api of milestone 148 cannot point graphite at a drawable

#include "prototypes/a1/graphite_context.h"

#import <Metal/Metal.h>

#include <chrono>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/graphite/Context.h>
#include <skia/gpu/graphite/ContextOptions.h>
#include <skia/gpu/graphite/Recorder.h>
#include <skia/gpu/graphite/Surface.h>
#include <skia/gpu/graphite/mtl/MtlBackendContext.h>

#include "prototypes/a1/custom_render_node.h"
#include "prototypes/a1/resource_set.h"

namespace albor::a1
{

    namespace
    {

        // the clock this slice measures with, monotonic like every other slice (§7.1)
        using Clock = std::chrono::steady_clock;

        // read the clock as milliseconds since the steady clock epoch
        double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

    } // namespace

    struct GraphiteContext::State
    {
        // the metal objects graphite records against, which the process owns for its whole life
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;

        // the graphite context, its recorder, and the surface the current frame records into
        std::unique_ptr<skgpu::graphite::Context> context;
        std::unique_ptr<skgpu::graphite::Recorder> recorder;
        sk_sp<SkSurface> surface;

        // the pixel size the offscreen surface is allocated at, which must match the recorded window size
        std::size_t pixel_width = 0;
        std::size_t pixel_height = 0;

        // what the current frame has cost so far
        std::size_t frames_submitted = 0;
        std::uint64_t frame_index = 0;
        GraphiteFrameTiming timing{};
    };

    GraphiteContext::GraphiteContext() :
        m_state(new State()) {}

    GraphiteContext::~GraphiteContext() {
        // the last frame's recording was already inserted and submitted by end_frame, so there is nothing to
        // submit here. an empty submit would only make skia log 'no active command buffer'

        // release the renderer state
        delete m_state;
        m_state = nullptr;
    }

    bool GraphiteContext::create(std::size_t pixel_width, std::size_t pixel_height) {
        // a surface needs a size
        if (pixel_width == 0 || pixel_height == 0) {
            return false;
        }
        m_state->pixel_width = pixel_width;
        m_state->pixel_height = pixel_height;

        // resolve the metal device and queue directly, because this slice needs no window and no window server
        m_state->device = MTLCreateSystemDefaultDevice();
        if (m_state->device == nil) {
            return false;
        }
        m_state->queue = [m_state->device newCommandQueue];
        if (m_state->queue == nil) {
            return false;
        }

        // hand graphite the device and the queue; the cfp ctor adopts without retaining, and this process owns
        // both objects for its whole life, so nothing here has to balance a retain
        skgpu::graphite::MtlBackendContext backend;
        backend.fDevice = sk_cfp<CFTypeRef>(static_cast<CFTypeRef>(m_state->device));
        backend.fQueue = sk_cfp<CFTypeRef>(static_cast<CFTypeRef>(m_state->queue));

        // bring up the graphite metal backend, which is what the port's graphite feature built
        m_state->context = skgpu::graphite::ContextFactory::MakeMetal(backend, skgpu::graphite::ContextOptions{});
        if (m_state->context == nullptr) {
            return false;
        }

        // every frame records through one recorder, which is the object a1.3 had no equivalent for
        m_state->recorder = m_state->context->makeRecorder();
        return m_state->recorder != nullptr;
    }

    bool GraphiteContext::begin_frame() {
        // a frame needs a context and a recorder
        if (m_state->context == nullptr || m_state->recorder == nullptr) {
            return false;
        }

        // allocate the surface this frame records into, and measure that allocation on its own because it is the
        // cost the presented path does not have
        const double surface_start = now_ms();
        m_state->surface = SkSurfaces::RenderTarget(m_state->recorder.get(), SkImageInfo::Make(static_cast<int>(m_state->pixel_width), static_cast<int>(m_state->pixel_height), SkColorType::kBGRA_8888_SkColorType, SkAlphaType::kPremul_SkAlphaType), skgpu::Mipmapped::kNo);

        // reset the frame's measurements
        m_state->timing = GraphiteFrameTiming{};
        m_state->timing.index = m_state->frame_index;
        m_state->timing.surface_ms = now_ms() - surface_start;
        return m_state->surface != nullptr;
    }

    void GraphiteContext::draw(CustomRenderNode& node) {
        // nothing to draw into means the frame never started
        if (m_state->surface == nullptr) {
            return;
        }

        // the retained set is immutable, so reading it here runs no application code (§10.2)
        const ResourceSetPtr& resources = node.resources();
        if (resources == nullptr) {
            return;
        }

        // start the recording clock, which is what the record cost is measured against
        const double record_start = now_ms();
        SkCanvas* canvas = m_state->surface->getCanvas();

        // clear the frame, because the retained geometry is additive over a dark background
        canvas->clear(SK_ColorBLACK);

        // apply the view transform to the canvas, exactly as the ganesh path does, so the two are comparable
        canvas->save();
        canvas->translate(node.view().offset_x, node.view().offset_y);
        canvas->scale(node.view().scale, node.view().scale);

        // replay the same retained batches the ganesh path replays, one draw call per batch
        for (const sk_sp<SkVertices>& batch : resources->line_batches) {
            canvas->drawVertices(batch, SkBlendMode::kSrcOver, resources->line_paint);
        }

        // restore the canvas, so the next frame starts from the identity transform
        canvas->restore();
        m_state->timing.record_ms = now_ms() - record_start;
    }

    void GraphiteContext::end_frame() {
        // a frame that never began has nothing to submit
        if (m_state->surface == nullptr || m_state->context == nullptr) {
            return;
        }

        // a graphite recorder is not submitted by context->submit(). the recording has to be snapped off the
        // recorder and inserted into the context, or the submit has no command buffer and nothing reaches the gpu.
        // this omission is why a1.4's submit and frame figures described work that never ran, and why the corrected
        // graphite+dawn prototype logs 'Submit called with no active command buffer!' per frame without it
        std::unique_ptr<skgpu::graphite::Recording> recording = m_state->recorder->snap();
        if (recording != nullptr) {
            skgpu::graphite::InsertRecordingInfo insert;
            insert.fRecording = recording.release();
            m_state->context->insertRecording(insert);
        }

        // start the submit clock, which is what a1.4 compares against ganesh
        const double submit_start = now_ms();
        m_state->context->submit();
        m_state->timing.submit_ms = now_ms() - submit_start;

        // the gpu work has to be waited on before the surface goes away, or the recorded ops lose their target. this
        // is a cpu wait for gpu completion, not a gpu timestamp, and it is why a1.4's submit figure was meaningless:
        // an enqueue that was never waited on
        const double fence_start = now_ms();
        m_state->context->submit(skgpu::graphite::SubmitInfo{skgpu::graphite::SyncToCpu::kYes});
        m_state->timing.fence_ms = now_ms() - fence_start;

        // the whole frame is what the throughput claim rests on
        m_state->timing.frame_ms = m_state->timing.surface_ms + m_state->timing.record_ms + m_state->timing.submit_ms;
        m_state->frames_submitted++;
        m_state->frame_index++;

        // let the surface go before the next frame allocates one
        m_state->surface = nullptr;
    }

    std::size_t GraphiteContext::frames_submitted() const { return m_state->frames_submitted; }

    const GraphiteFrameTiming& GraphiteContext::last_timing() const { return m_state->timing; }

    const char* GraphiteContext::backend() const {
        // report the backend that actually came up, because §11.7 compares backends
        return m_state->context == nullptr ? "none" : "graphite-metal";
    }

} // namespace albor::a1
