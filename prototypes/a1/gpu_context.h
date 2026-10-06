#pragma once

#include <cstddef>
#include <cstdint>

#include "prototypes/a2/platform.h"

namespace albor::a1
{

    class CustomRenderNode;

    // what one frame of the skia path cost, split the way §28.2 asks for (§9.2, §11.7)
    struct GpuFrameTiming
    {
        std::uint64_t index = 0;
        double acquire_ms = 0.0; // wrapping the layer, which waits for the compositor's drawable
        double record_ms = 0.0; // replaying the retained resources into the canvas
        double submit_ms = 0.0; // flushing and submitting the frame
        double hold_ms = 0.0; // the injected presentation hold, which is zero unless a workload asked for one
        double interval_ms = 0.0; // time since the previous submitted frame
    };

    // the gpu context of one window: it owns the renderer, wraps the platform surface and submits frames (§9.2)
    // no skia type and no platform type appears in this header (AD-05, AD-19)
    class GpuContext
    {
    public:
        GpuContext();
        ~GpuContext();

        GpuContext(const GpuContext&) = delete;
        GpuContext& operator=(const GpuContext&) = delete;
        GpuContext(GpuContext&&) = delete;
        GpuContext& operator=(GpuContext&&) = delete;

        // create the renderer against the metal device and queue of the window
        [[nodiscard]] bool create(albor::a2::PlatformWindow& window);

        // start a frame, which wraps the platform surface and therefore may wait for a drawable
        [[nodiscard]] bool begin_frame();

        // draw the retained resources of a node through its view transform, without any application code (§10.2)
        void draw(CustomRenderNode& node);

        // flush and submit the frame, and apply the injected hold when one is configured
        void end_frame();

        // hold the presenting thread for the given time after every submit, which is how w3 injects a gpu stall
        void set_presentation_hold_ms(double hold_ms);

        // the backend the renderer actually came up with, which is the §11.7 backend question
        [[nodiscard]] const char* backend() const;

        [[nodiscard]] std::size_t frames_submitted() const;

        [[nodiscard]] const GpuFrameTiming& last_timing() const;

    private:
        struct State;
        State* m_state = nullptr;
    };

} // namespace albor::a1
