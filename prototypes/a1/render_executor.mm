#include "prototypes/a1/render_executor.h"

#import "prototypes/a2/native_surface_mac.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

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

        // how many scenes may wait for the render thread before the ui thread drops the oldest one (§10.3)
        constexpr std::size_t MAX_PENDING_SCENES = 3;

        // how many frames may be in flight before the ui thread stops asking for a drawable, which is the
        // bounded in flight budget of the frame scheduler and the reason the ui thread never waits (§10.3)
        constexpr std::size_t MAX_IN_FLIGHT_FRAMES = 3;

        // one queued scene with the drawable the ui thread acquired for it and the timings it measured
        struct Envelope
        {
            FrameScene scene;
            id<CAMetalDrawable> drawable = nil;
            Clock::time_point submitted_at;
            double build_ms = 0.0;
        };

    } // namespace

    struct RenderExecutor::State
    {
        albor::a2::NativeSurface* surface = nullptr;
        std::thread worker;
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<Envelope> pending;
        std::vector<FrameTiming> recorded;
        Clock::time_point last_present{};
        bool has_last_present = false;
        std::size_t frames_executed = 0;
        std::size_t frames_dropped = 0;
        std::size_t frames_coalesced = 0;
        std::atomic<std::size_t> in_flight{0};
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

        // the executor needs a device and a queue to present with
        if (m_state->surface == nullptr || m_state->surface->layer == nil || m_state->surface->queue == nil) {
            return false;
        }

        // start the render thread that owns stage 2 (§10.2)
        m_state->running = true;
        m_state->worker = std::thread([state = m_state] {
            while (state->running) {
                // take one scene off the queue, waiting for one when the queue is empty
                Envelope envelope;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->ready.wait(lock, [state] { return !state->running || !state->pending.empty(); });

                    // leave the loop once the executor was stopped and the queue drained
                    if (state->pending.empty()) {
                        if (!state->running) {
                            return;
                        }
                        continue;
                    }

                    envelope = std::move(state->pending.front());
                    state->pending.pop_front();
                }

                // stage 2 never calls application code and never touches the layer, it only executes the immutable scene (§10.2)
                const Clock::time_point start = Clock::now();
                id<MTLCommandBuffer> commands = [state->surface->queue commandBuffer];

                if (envelope.drawable != nil && commands != nil) {
                    // describe the single attachment this phase clears
                    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                    MTLRenderPassColorAttachmentDescriptor* color = pass.colorAttachments[0];
                    color.texture = envelope.drawable.texture;
                    color.loadAction = MTLLoadActionClear;
                    color.storeAction = MTLStoreActionStore;
                    color.clearColor = MTLClearColorMake(0.05, 0.06, 0.08, 1.0);

                    // encode and present the frame without waiting for the gpu, because the compositor paces it
                    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
                    encoder.label = @"albor-a1-clear";
                    [encoder endEncoding];
                    [commands presentDrawable:envelope.drawable];
                    [commands commit];
                }

                // the drawable is not needed once the frame was submitted to the compositor
                [envelope.drawable release];

                // release the in flight slot so the ui thread may ask for another drawable
                state->in_flight--;

                const Clock::time_point done = Clock::now();
                const std::lock_guard<std::mutex> lock(state->mutex);

                // record what the frame cost on both sides of the handoff
                FrameTiming timing;
                timing.index = envelope.scene.index;
                timing.build_ms = envelope.build_ms;
                timing.present_ms = to_ms(done - start);
                timing.submit_to_present_ms = to_ms(done - envelope.submitted_at);
                timing.interval_ms = state->has_last_present ? to_ms(done - state->last_present) : 0.0;
                state->last_present = done;
                state->has_last_present = true;
                state->recorded.push_back(timing);
                state->frames_executed++;
            }
        });

        // report that the render thread is running
        return true;
    }

    double RenderExecutor::submit(FrameScene scene) {
        // the ui thread drives the layer, because cametal layer drawables are only produced for the thread that owns it
        const Clock::time_point start = Clock::now();
        m_state->update_layer_geometry(scene.metrics);

        // stop asking for a drawable while the in flight budget is spent, because coalescing a frame is
        // what the frame scheduler requires and blocking the ui thread is what it forbids (§10.3)
        if (m_state->in_flight.load() >= MAX_IN_FLIGHT_FRAMES) {
            const std::lock_guard<std::mutex> busy_lock(m_state->mutex);
            m_state->frames_coalesced++;
            return to_ms(Clock::now() - start);
        }

        id<CAMetalDrawable> drawable = [m_state->surface->layer nextDrawable];

        // retain the drawable explicitly, because a c++ container does not hold an objective c reference
        if (drawable != nil) {
            [drawable retain];
        }

        // count the frame as in flight so the ui thread stops asking when the budget is spent
        m_state->in_flight++;

        const std::lock_guard<std::mutex> lock(m_state->mutex);

        // drop the oldest scene when the queue is full, because coalescing beats blocking the ui thread (§10.3)
        if (m_state->pending.size() >= MAX_PENDING_SCENES) {
            // release the drawable of the dropped scene, because nothing else owns it
            Envelope dropped = std::move(m_state->pending.front());
            m_state->pending.pop_front();
            [dropped.drawable release];
            m_state->in_flight--;
            m_state->frames_dropped++;
        }

        // hand the immutable scene and its drawable to stage 2
        Envelope envelope;
        envelope.scene = std::move(scene);
        envelope.drawable = drawable;
        envelope.submitted_at = start;
        m_state->pending.push_back(std::move(envelope));

        // wake the render thread
        m_state->ready.notify_one();

        // report how long the ui thread was blocked by the handoff
        return to_ms(Clock::now() - start);
    }

    void RenderExecutor::stop() {
        // tell the render thread to finish and wait for it
        {
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            if (!m_state->running) {
                return;
            }
            m_state->running = false;
        }

        // wake the render thread so it observes the stop request
        m_state->ready.notify_all();

        // join the thread when it was started
        if (m_state->worker.joinable()) {
            m_state->worker.join();
        }
    }

    std::size_t RenderExecutor::frames_executed() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_executed;
    }

    std::size_t RenderExecutor::frames_coalesced() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_coalesced;
    }

    std::size_t RenderExecutor::frames_dropped() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_dropped;
    }

    std::vector<FrameTiming> RenderExecutor::timings() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->recorded;
    }

} // namespace albor::a1
