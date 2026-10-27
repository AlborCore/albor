#pragma once

#include <chrono>
#include <cstdint>

namespace albor
{
    // a stopwatch a profiling path reads, which is live only when ALBOR_PROFILE names a non zero value (§6.1)
    class FrameProfileScope
    {
    public:
        // the scope starts at construction, which the elapsed read is measured against (§6.1)
        FrameProfileScope();

        // the milliseconds since the scope started, which a profiling call records (§6.1)
        [[nodiscard]] double elapsed_ms() const;

    private:
        // the instant the scope started, which every elapsed read subtracts (§6.1)
        std::chrono::steady_clock::time_point m_start;
    };

    // record one ui tick, whose interval is the ui thread's frame cadence (§6.1)
    void profile_ui_tick();

    // record one ui frame, split into the scene build and the submit (§6.1)
    void profile_ui_frame(double build_ms, double submit_ms);

    // record one rendered frame, which is the whole stage two call (§6.1, AD-18)
    void profile_render_frame(double total_ms);

    // record one backend present, which is the encode and the drawable wait (§6.3)
    void profile_present(double present_ms);

    // record the texture upload one present spent, which is its first phase (§6.3)
    void profile_upload(double upload_ms);

    // record the draw encode one present spent, which is its second phase (§6.3)
    void profile_encode(double encode_ms);

    // record the blit and commit one present spent, which is its last phase (§6.3)
    void profile_commit(double commit_ms);

    // record one wait for a drawable, which is the compositor pacing inside the present (§6.3)
    void profile_drawable_wait(double wait_ms);

    // record the batch one frame carries, which sizes the geometry the backend tessellated (§6.3)
    void profile_batch(std::uint64_t vertices, std::uint64_t triangles, std::uint64_t draws, std::uint64_t shadows, std::uint64_t meshes = 0);

    // record one submission that replaced a pending frame, which is a frame the ui thread dropped (§6.1)
    void profile_coalesced();
} // namespace albor
