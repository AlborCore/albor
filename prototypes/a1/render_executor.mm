#include "prototypes/a1/render_executor.h"

#import "prototypes/a2/native_surface_mac.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace albor::a1
{

    namespace
    {

        // the clock both stages measure with
        using Clock = std::chrono::steady_clock;

        // convert a duration into milliseconds
        double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

        // the in flight budget of the frame scheduler: drawables that may be pending at once (§10.3)
        constexpr std::size_t MAX_IN_FLIGHT_DRAWABLES = 2;

    } // namespace

    // the queue slot that crosses from the ui thread to the encoding worker
    struct RenderWorkItem
    {
        std::uint64_t frame_index = 0;
        FrameScene scene;
        albor::a2::NativeSurface* surface = nullptr;
        id<CAMetalDrawable> drawable = nil;
        Clock::time_point submitted_at;
        double build_ms = 0.0;
    };

    // the encoded command buffer that crosses back from the worker to the presenting thread
    struct RenderEncodedItem
    {
        std::uint64_t frame_index = 0;
        id<MTLCommandBuffer> commands = nil;
        albor::a2::NativeSurface* surface = nullptr;
        id<CAMetalDrawable> drawable = nil;
        Clock::time_point submitted_at;
        double build_ms = 0.0;
        double encode_ms = 0.0;
    };

    struct RenderExecutor::State
    {
        albor::a2::NativeSurface* surface = nullptr;
        std::thread worker;
        std::mutex mutex;
        std::condition_variable to_worker;
        std::condition_variable to_ui;
        std::deque<RenderWorkItem> pending;
        std::deque<RenderEncodedItem> encoded;
        std::vector<FrameTiming> recorded;
        Clock::time_point last_present{};
        bool has_last_present = false;
        std::size_t frames_executed = 0;
        std::size_t frames_dropped = 0;
        std::size_t frames_coalesced = 0;
        std::atomic<std::size_t> in_flight{0};
        double last_build_ms = 0.0;
        bool running = false;
        albor::a2::Metrics last_geometry{};

        // update the layer only when the geometry changed, because touching it invalidates its drawables
        void update_layer_geometry(const albor::a2::Metrics& metrics) {
            // skip the update when nothing changed
            if (metrics.logical_width == last_geometry.logical_width && metrics.logical_height == last_geometry.logical_height && metrics.scale_factor == last_geometry.scale_factor) {
                return;
            }

            // remember the geometry the layer now has
            last_geometry = metrics;

            // apply the logical bounds and the device scale to the layer
            const CGFloat scale = static_cast<CGFloat>(metrics.scale_factor);
            const CGFloat width = static_cast<CGFloat>(metrics.logical_width);
            const CGFloat height = static_cast<CGFloat>(metrics.logical_height);
            surface->layer.frame = CGRectMake(0, 0, width, height);
            surface->layer.contentsScale = scale;
            surface->layer.drawableSize = CGSizeMake(width * scale, height * scale);
        }
    };

    RenderExecutor::RenderExecutor() :
        m_state(new State()) {}

    RenderExecutor::~RenderExecutor() {
        // make sure the render thread is gone before the state is released
        stop();
        delete m_state;
        m_state = nullptr;
    }

    bool RenderExecutor::start(albor::a2::PlatformWindow& window) {
        // resolve the native surface of the window
        m_state->surface = window.native_surface();

        // the executor needs a device and a queue to record with
        if (m_state->surface == nullptr || m_state->surface->layer == nil || m_state->surface->queue == nil) {
            return false;
        }

        // start the worker that only records command buffers, which is what stage 2 may do off the ui thread (§10.2)
        m_state->running = true;
        m_state->worker = std::thread([state = m_state] {
            while (state->running) {
                // take one scene off the queue, waiting for one when the queue is empty
                RenderWorkItem work;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->to_worker.wait(lock, [state] { return !state->running || !state->pending.empty(); });

                    // leave the loop once the executor was stopped and the queue drained
                    if (state->pending.empty()) {
                        if (!state->running) {
                            return;
                        }
                        continue;
                    }

                    work = std::move(state->pending.front());
                    state->pending.pop_front();
                }

                // record one command buffer on the worker, without presenting it (§10.2)
                const Clock::time_point start = Clock::now();
                id<MTLCommandBuffer> commands = [work.surface->queue commandBuffer];

                if (work.drawable != nil && commands != nil) {
                    // describe the single attachment this phase clears
                    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                    MTLRenderPassColorAttachmentDescriptor* color = pass.colorAttachments[0];
                    color.texture = work.drawable.texture;
                    color.loadAction = MTLLoadActionClear;
                    color.storeAction = MTLStoreActionStore;
                    color.clearColor = MTLClearColorMake(0.05, 0.06, 0.08, 1.0);

                    // encode the passes, which is the only work the worker performs
                    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
                    encoder.label = @"albor-a1-encode";
                    [encoder endEncoding];
                }

                {
                    // hand the encoded buffer to the thread that presents
                    const std::lock_guard<std::mutex> lock(state->mutex);
                    RenderEncodedItem item;
                    item.frame_index = work.frame_index;
                    item.commands = commands;
                    item.surface = work.surface;
                    item.drawable = work.drawable;
                    item.submitted_at = work.submitted_at;
                    item.build_ms = work.build_ms;
                    item.encode_ms = to_ms(Clock::now() - start);
                    state->encoded.push_back(item);
                }

                // wake the ui thread so it can present
                state->to_ui.notify_one();
            }
        });

        // report that the recording worker is running
        return true;
    }

    double RenderExecutor::submit(FrameScene scene) {
        // the present path starts on the ui thread, because cametal layer drawables belong to the thread that drives it
        const Clock::time_point start = Clock::now();

        // present whatever the worker finished, because presentation stays pinned to this thread
        present_ready(start);

        // stop asking for a drawable while the in flight budget is spent, because coalescing is what the
        // frame scheduler requires and blocking the ui thread is what it forbids (§10.3)
        if (m_state->in_flight.load() >= MAX_IN_FLIGHT_DRAWABLES) {
            const std::lock_guard<std::mutex> busy_lock(m_state->mutex);
            m_state->frames_coalesced++;
            return to_ms(Clock::now() - start);
        }

        // keep the layer in step with the window before asking for a drawable
        m_state->update_layer_geometry(scene.metrics);

        // acquire the drawable, which is the only blocking call the ui thread still makes
        id<CAMetalDrawable> drawable = [m_state->surface->layer nextDrawable];

        // retain the drawable explicitly, because a c++ container does not hold an objective c reference
        if (drawable != nil) {
            [drawable retain];
        }

        // count the frame as in flight so the ui thread stops asking when the budget is spent
        m_state->in_flight++;

        const std::lock_guard<std::mutex> lock(m_state->mutex);

        // hand the immutable scene and its drawable to the recording worker
        RenderWorkItem work;
        work.frame_index = scene.index;
        work.scene = std::move(scene);
        work.surface = m_state->surface;
        work.drawable = drawable;
        work.submitted_at = start;
        work.build_ms = m_state->last_build_ms;
        m_state->pending.push_back(std::move(work));

        // wake the worker
        m_state->to_worker.notify_one();

        // report how long the ui thread was blocked
        return to_ms(Clock::now() - start);
    }

    void RenderExecutor::present_ready(std::chrono::steady_clock::time_point tick_start) {
        // take everything the worker finished, without waiting for it
        std::deque<RenderEncodedItem> ready;
        {
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            ready.swap(m_state->encoded);
        }

        // present each encoded buffer on this thread, which is where the layer lives
        for (RenderEncodedItem& item : ready) {
            // present and submit the frame
            if (item.commands != nil && item.drawable != nil) {
                [item.commands presentDrawable:item.drawable];
                [item.commands commit];
            }

            // the drawable is not needed once the frame was submitted to the compositor
            [item.drawable release];

            // release the in flight slot so the ui thread may ask for another drawable
            m_state->in_flight--;

            const Clock::time_point done = Clock::now();
            const std::lock_guard<std::mutex> lock(m_state->mutex);

            // record what the frame cost on both sides of the handoff
            FrameTiming timing;
            timing.index = item.build_ms > 0.0 ? item.frame_index : 0;
            timing.build_ms = item.build_ms;
            timing.present_ms = to_ms(done - tick_start);
            timing.submit_to_present_ms = to_ms(done - item.submitted_at);
            timing.interval_ms = m_state->has_last_present ? to_ms(done - m_state->last_present) : 0.0;
            m_state->last_present = done;
            m_state->has_last_present = true;
            m_state->recorded.push_back(timing);
            m_state->frames_executed++;
        }
    }

    void RenderExecutor::stop() {
        // tell the worker to finish and wait for it
        {
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            if (!m_state->running) {
                return;
            }
            m_state->running = false;
        }

        // wake the worker so it observes the stop request
        m_state->to_worker.notify_all();

        // join the thread when it was started
        if (m_state->worker.joinable()) {
            m_state->worker.join();
        }
    }

    void RenderExecutor::set_build_ms(double build_ms) { m_state->last_build_ms = build_ms; }

    std::size_t RenderExecutor::frames_executed() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_executed;
    }

    std::size_t RenderExecutor::frames_dropped() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_dropped;
    }

    std::size_t RenderExecutor::frames_coalesced() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_coalesced;
    }

    std::vector<FrameTiming> RenderExecutor::timings() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->recorded;
    }

} // namespace albor::a1
