#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "prototypes/a2/platform.h"

namespace albor::a1
{

    // the immutable scene that stage 1 produces and stage 2 executes (§10.2)
    struct FrameScene
    {
        std::uint64_t index = 0;
        albor::a2::Metrics metrics;
        std::vector<float> vertices;
    };

    // what one frame cost, measured on both sides of the handoff (§28.2)
    struct FrameTiming
    {
        std::uint64_t index = 0;
        double build_ms = 0.0; // stage 1 on the ui thread
        double submit_blocked_ms = 0.0; // how long the ui thread waited to hand the scene over
        double encode_ms = 0.0; // stage 2 recording on the worker
        double present_ms = 0.0; // presenting on the ui thread
        double submit_to_present_ms = 0.0; // end to end across the handoff
        double interval_ms = 0.0; // time between consecutive presents
    };

    // the queue slot that crosses from the ui thread to the recording worker
    struct RenderWorkItem;

    // the encoded command buffer that crosses back from the worker to the presenting thread
    struct RenderEncodedItem;

    // the render executor that owns recording and presentation and never runs application code (§10.2)
    class RenderExecutor
    {
    public:
        RenderExecutor();
        ~RenderExecutor();

        RenderExecutor(const RenderExecutor&) = delete;
        RenderExecutor& operator=(const RenderExecutor&) = delete;
        RenderExecutor(RenderExecutor&&) = delete;
        RenderExecutor& operator=(RenderExecutor&&) = delete;

        // start the render thread against the native layer of a window
        [[nodiscard]] bool start(albor::a2::PlatformWindow& window);

        // hand an immutable scene to stage 2, returning how long the ui thread was blocked
        // remember how long stage 1 took, so the record carries the scene build cost
        void set_build_ms(double build_ms);

        // hand an immutable scene to stage 2, returning how long the ui thread was blocked
        double submit(FrameScene scene);

        // stop the render thread and wait for it to drain
        void stop();

        [[nodiscard]] std::size_t frames_executed() const;
        [[nodiscard]] std::size_t frames_dropped() const;

        // frames that were coalesced because the layer had no drawable to give (§10.3)
        [[nodiscard]] std::size_t frames_coalesced() const;
        [[nodiscard]] std::vector<FrameTiming> timings() const;

    private:
        struct State;

        // present everything the recording worker finished, on the thread that owns the layer
        void present_ready(std::chrono::steady_clock::time_point tick_start);
        State* m_state = nullptr;
    };

} // namespace albor::a1
