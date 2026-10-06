#pragma once

#include <cstddef>
#include <cstdint>

namespace albor::a1
{

    class CustomRenderNode;

    // what one offscreen graphite frame cost, split the way a1.4 has to compare it against ganesh (§28.2)
    struct GraphiteFrameTiming
    {
        std::uint64_t index = 0;
        double surface_ms = 0.0; // allocating the offscreen surface for this frame
        double record_ms = 0.0; // replaying the retained resources into the canvas
        double submit_ms = 0.0; // handing the recorded work to the gpu
        double fence_ms = 0.0; // the cpu wait for gpu completion, which is not a gpu timestamp
        double frame_ms = 0.0; // surface + record + enqueue, the cpu-side whole frame
    };

    // the offscreen graphite context: it owns a graphite context, a recorder and one reusable surface (§11.7)
    // no skia type and no platform type appears in this header (AD-05, AD-19)
    class GraphiteContext
    {
    public:
        GraphiteContext();
        ~GraphiteContext();

        GraphiteContext(const GraphiteContext&) = delete;
        GraphiteContext& operator=(const GraphiteContext&) = delete;
        GraphiteContext(GraphiteContext&&) = delete;
        GraphiteContext& operator=(GraphiteContext&&) = delete;

        // bring up the metal backend and allocate the offscreen surface of the requested pixel size
        [[nodiscard]] bool create(std::size_t pixel_width, std::size_t pixel_height);

        // start a frame, which allocates the surface the frame records into
        [[nodiscard]] bool begin_frame();

        // replay the retained resources of a node through its view transform (§11.2)
        void draw(CustomRenderNode& node);

        // submit the frame, which is the cost a1.4 compares against ganesh
        void end_frame();

        [[nodiscard]] std::size_t frames_submitted() const;

        [[nodiscard]] const GraphiteFrameTiming& last_timing() const;

        // the backend that actually came up, because §11.7 compares backends
        [[nodiscard]] const char* backend() const;

    private:
        struct State;
        State* m_state = nullptr;
    };

} // namespace albor::a1
