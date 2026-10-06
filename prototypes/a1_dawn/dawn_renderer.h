#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "prototypes/a1_dawn/resources.h"
#include "prototypes/a2/platform.h"

namespace albor::a1_dawn
{

    // what one dawn frame cost, split the way a1.3 split the skia frame so the two are comparable (§28.2)
    struct FrameTiming
    {
        std::uint64_t index = 0;
        double acquire_ms = 0.0; // getting the next texture out of the surface
        double record_ms = 0.0; // encoding the render pass and submitting it
        double present_ms = 0.0; // presenting the frame
        double interval_ms = 0.0; // time between consecutive presents
        double upload_ms = 0.0; // writing new per-instance data into the vertex buffer
        double hold_ms = 0.0; // the injected presentation hold, zero unless a workload asked for one
    };

    // the dawn context of one window: it owns the device, the surface and one instanced line pipeline
    // no dawn or webgpu type and no platform type appears in this header (AD-05, AD-06)
    class DawnContext
    {
    public:
        DawnContext();
        ~DawnContext();

        DawnContext(const DawnContext&) = delete;
        DawnContext& operator=(const DawnContext&) = delete;
        DawnContext(DawnContext&&) = delete;
        DawnContext& operator=(DawnContext&&) = delete;

        // create the device and configure the surface against the window's metal layer
        [[nodiscard]] bool create(albor::a2::PlatformWindow& window);

        // allocate a vertex buffer that can hold the largest batch the workloads will replay
        [[nodiscard]] bool reserve_instances(std::size_t instances);

        // upload the per-instance data of a retained set, writing only the tail when the set grew
        [[nodiscard]] bool upload(const ResourceSet& set, std::size_t already_written);

        // start a frame, which acquires the surface texture
        [[nodiscard]] bool begin_frame();

        // draw the retained set as one instanced call, which is what webgpu allows and skia does not
        void draw(const ViewTransform& view, std::size_t instances);

        // present the frame
        void end_frame();

        // hold the presenting thread for the given time after presenting, which is how w3 injects a stall
        void set_presentation_hold_ms(double hold_ms);

        [[nodiscard]] std::size_t frames_presented() const;

        [[nodiscard]] const FrameTiming& last_timing() const;

        [[nodiscard]] std::size_t draw_calls() const;

        [[nodiscard]] const char* backend() const;

    private:
        struct State;
        State* m_state = nullptr;
    };

} // namespace albor::a1_dawn
