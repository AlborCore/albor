#include "render/frame_profile.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace albor
{
    FrameProfileScope::FrameProfileScope() :
        m_start(std::chrono::steady_clock::now()) {}

    double FrameProfileScope::elapsed_ms() const {
        // the elapsed span is what a profiling call records, in the unit the report prints (§6.1)
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    }

    namespace
    {
        // a frame slower than this gets its own line, which is where a display period is already missed (§6.1)
        constexpr double SLOW_FRAME_MS = 25.0;

        // one second of samples, which a report prints and then clears (§6.1)
        struct ProfileWindow
        {
            // the instant the window started, which the report interval is measured from
            std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

            // the instant of the last ui tick, which the next tick gap is measured from
            std::chrono::steady_clock::time_point last_tick = std::chrono::steady_clock::now();

            // the ui ticks the window saw, which is the cadence the display link asked for
            std::uint64_t ticks = 0;

            // the summed and the longest gap between two ui ticks, in milliseconds
            double tick_sum = 0.0;

            // the longest ui tick gap, in milliseconds
            double tick_max = 0.0;

            // the frames the ui thread built and handed to stage two
            std::uint64_t ui_frames = 0;

            // the summed scene build time, in milliseconds
            double build_sum = 0.0;

            // the longest scene build, in milliseconds
            double build_max = 0.0;

            // the summed submit time, in milliseconds
            double submit_sum = 0.0;

            // the longest submit, in milliseconds
            double submit_max = 0.0;

            // the frames stage two executed
            std::uint64_t render_frames = 0;

            // the summed whole stage two time, in milliseconds
            double render_sum = 0.0;

            // the longest whole stage two time, in milliseconds
            double render_max = 0.0;

            // the presents the backend reported
            std::uint64_t presents = 0;

            // the summed backend present time, in milliseconds
            double present_sum = 0.0;

            // the longest backend present, in milliseconds
            double present_max = 0.0;

            // the uploads the backend reported
            std::uint64_t uploads = 0;

            // the summed texture upload time, in milliseconds
            double upload_sum = 0.0;

            // the longest texture upload, in milliseconds
            double upload_max = 0.0;

            // the encodes the backend reported
            std::uint64_t encodes = 0;

            // the summed draw encode time, in milliseconds
            double encode_sum = 0.0;

            // the longest draw encode, in milliseconds
            double encode_max = 0.0;

            // the commits the backend reported
            std::uint64_t commits = 0;

            // the summed blit and commit time, in milliseconds
            double commit_sum = 0.0;

            // the longest blit and commit, in milliseconds
            double commit_max = 0.0;

            // the waits the backend reported
            std::uint64_t waits = 0;

            // the summed drawable wait, in milliseconds
            double wait_sum = 0.0;

            // the longest drawable wait, in milliseconds
            double wait_max = 0.0;

            // the last present, which a slow render line quotes
            double last_present = 0.0;

            // the last drawable wait, which a slow present line quotes
            double last_wait = 0.0;

            // the last upload, encode and commit, which a slow render line splits the present by
            double last_upload = 0.0;
            double last_encode = 0.0;
            double last_commit = 0.0;

            // the submissions that replaced a pending frame, which are the frames the ui thread dropped
            std::uint64_t coalesced = 0;

            // the vertices the last batch carried, which is the geometry the backend tessellated
            std::uint64_t batch_vertices = 0;

            // the triangles the last batch carried, which is what the gpu rasterized
            std::uint64_t batch_triangles = 0;

            // the draws the last batch carried, which is what one command buffer encoded
            std::uint64_t batch_draws = 0;

            // the shadows the last batch carried, which are the offscreen passes the present composites
            std::uint64_t batch_shadows = 0;

            // the expanded glyph meshes the surface retained, which shows whether a frame swept a new glyph
            std::uint64_t batch_meshes = 0;
        };

        // read the environment once, because the gate is what keeps a normal run silent (§6.1)
        [[nodiscard]] bool read_enabled() {
            const char* value = std::getenv("ALBOR_PROFILE");
            // an unset or empty variable leaves the profile off, which is what a normal run carries
            if (value == nullptr || value[0] == '\0')
                return false;
            // a leading zero is the explicit off, so a script can export the variable without profiling
            return value[0] != '0';
        }

        // the process wide state, which the ui thread and the render thread both record into (§6.1)
        struct ProfileState
        {
            // the gate is read once, because every recorder checks it before it touches the window
            ProfileState() {
                // the environment decides the gate, which a normal run leaves unset
                enabled = read_enabled();
                // the header line confirms the gate at startup, because every later line depends on it
                if (enabled)
                    std::fprintf(stderr, "[profile] frame profile on\n");
            }

            // the lock the two frame threads share, which protects the window below
            std::mutex mutex;

            // the second that is being collected, which a report clears
            ProfileWindow window;

            // whether ALBOR_PROFILE asked for the profile, which is read once at first use
            bool enabled = false;
        };

        // the one state of the process, which a window stage and a backend share (§6.1)
        ProfileState& profile_state() {
            // the state is built on first use, because the render thread may record before the ui thread does
            static ProfileState state;
            return state;
        }

        // append one sample to its sum and its maximum, which every recorder does (§6.1)
        void add_sample(double value, double& sum, double& maximum) {
            sum += value;
            maximum = std::max(maximum, value);
        }

        // print the window that just ended and clear it, which keeps the collected second bounded (§6.1)
        void report_window(ProfileWindow& window) {
            // an idle second built no scene and rendered no frame, so it prints no line at all
            const bool idle = window.ui_frames == 0 && window.render_frames == 0;
            if (idle) {
                // the idle window is cleared silently, so a long wait never floods the log
                const std::chrono::steady_clock::time_point last_tick = window.last_tick;
                window = ProfileWindow{};
                window.last_tick = last_tick;
                return;
            }
            // a tick with no predecessor carries no interval, so the average divides by the gaps that exist
            const std::uint64_t intervals = window.ticks > 0 ? window.ticks - 1 : 0;
            const double tick_avg = intervals == 0 ? 0.0 : window.tick_sum / static_cast<double>(intervals);
            const double build_avg = window.ui_frames == 0 ? 0.0 : window.build_sum / static_cast<double>(window.ui_frames);
            const double submit_avg = window.ui_frames == 0 ? 0.0 : window.submit_sum / static_cast<double>(window.ui_frames);
            const double render_avg = window.render_frames == 0 ? 0.0 : window.render_sum / static_cast<double>(window.render_frames);
            const double present_avg = window.presents == 0 ? 0.0 : window.present_sum / static_cast<double>(window.presents);
            const double wait_avg = window.waits == 0 ? 0.0 : window.wait_sum / static_cast<double>(window.waits);
            const double upload_avg = window.uploads == 0 ? 0.0 : window.upload_sum / static_cast<double>(window.uploads);
            const double encode_avg = window.encodes == 0 ? 0.0 : window.encode_sum / static_cast<double>(window.encodes);
            const double commit_avg = window.commits == 0 ? 0.0 : window.commit_sum / static_cast<double>(window.commits);
            std::fprintf(stderr, "[profile] ticks=%llu tick_avg=%.2f tick_max=%.2f ui=%llu build_avg=%.2f build_max=%.2f submit_avg=%.2f submit_max=%.2f render=%llu render_avg=%.2f render_max=%.2f present=%llu present_avg=%.2f present_max=%.2f wait=%llu wait_avg=%.2f wait_max=%.2f upload=%llu upload_avg=%.2f upload_max=%.2f encode=%llu encode_avg=%.2f encode_max=%.2f commit=%llu commit_avg=%.2f commit_max=%.2f coalesced=%llu meshes=%llu verts=%llu tris=%llu draws=%llu shadows=%llu\n", static_cast<unsigned long long>(window.ticks), tick_avg, window.tick_max, static_cast<unsigned long long>(window.ui_frames), build_avg, window.build_max, submit_avg, window.submit_max, static_cast<unsigned long long>(window.render_frames), render_avg, window.render_max, static_cast<unsigned long long>(window.presents), present_avg, window.present_max, static_cast<unsigned long long>(window.waits), wait_avg, window.wait_max, static_cast<unsigned long long>(window.uploads), upload_avg, window.upload_max, static_cast<unsigned long long>(window.encodes), encode_avg, window.encode_max, static_cast<unsigned long long>(window.commits), commit_avg, window.commit_max, static_cast<unsigned long long>(window.coalesced), static_cast<unsigned long long>(window.batch_meshes), static_cast<unsigned long long>(window.batch_vertices), static_cast<unsigned long long>(window.batch_triangles), static_cast<unsigned long long>(window.batch_draws), static_cast<unsigned long long>(window.batch_shadows));
            // the tick cadence survives the report, so the next gap is measured from the real last tick
            const std::chrono::steady_clock::time_point last_tick = window.last_tick;
            window = ProfileWindow{};
            window.last_tick = last_tick;
        }

        // report once a second, which keeps a busy drag to one line per second (§6.1)
        void report_if_due(ProfileState& state, std::chrono::steady_clock::time_point now) {
            // a window younger than a second is still collecting, so it prints nothing yet
            const double age = std::chrono::duration<double>(now - state.window.started).count();
            if (age < 1.0)
                return;
            report_window(state.window);
            // the report instant starts the next window, so no sample is counted twice
            state.window.started = now;
        }
    } // namespace

    void profile_ui_tick() {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        // a tick with no predecessor only starts the cadence, so the first gap is never recorded
        if (state.window.ticks > 0) {
            const double gap = std::chrono::duration<double, std::milli>(now - state.window.last_tick).count();
            add_sample(gap, state.window.tick_sum, state.window.tick_max);
        }
        // the instant is kept for the next tick, which is what the gap is measured against
        state.window.last_tick = now;
        state.window.ticks++;
        report_if_due(state, now);
    }

    void profile_ui_frame(double build_ms, double submit_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.ui_frames++;
        add_sample(build_ms, state.window.build_sum, state.window.build_max);
        add_sample(submit_ms, state.window.submit_sum, state.window.submit_max);
        // a ui frame that overran a display period gets its own line, because a second average would hide it
        if (build_ms + submit_ms > SLOW_FRAME_MS)
            std::fprintf(stderr, "[profile] slow ui build=%.2f submit=%.2f total=%.2f\n", build_ms, submit_ms, build_ms + submit_ms);
        report_if_due(state, now);
    }

    void profile_render_frame(double total_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.render_frames++;
        add_sample(total_ms, state.window.render_sum, state.window.render_max);
        // a stage two call that overran a display period quotes its present, wait and present phases, which split the cost
        if (total_ms > SLOW_FRAME_MS)
            std::fprintf(stderr, "[profile] slow render total=%.2f present=%.2f wait=%.2f upload=%.2f encode=%.2f commit=%.2f meshes=%llu\n", total_ms, state.window.last_present, state.window.last_wait, state.window.last_upload, state.window.last_encode, state.window.last_commit, static_cast<unsigned long long>(state.window.batch_meshes));
        report_if_due(state, now);
    }

    void profile_present(double present_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.presents++;
        add_sample(present_ms, state.window.present_sum, state.window.present_max);
        // the value is kept so a slow render line can quote this frame's present
        state.window.last_present = present_ms;
        // a present that overran a display period quotes the wait it contained, which splits the cost
        if (present_ms > SLOW_FRAME_MS)
            std::fprintf(stderr, "[profile] slow present=%.2f wait=%.2f\n", present_ms, state.window.last_wait);
        report_if_due(state, now);
    }

    void profile_upload(double upload_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.uploads++;
        add_sample(upload_ms, state.window.upload_sum, state.window.upload_max);
        // the value is kept so a slow render line can split this frame's present
        state.window.last_upload = upload_ms;
        report_if_due(state, now);
    }

    void profile_encode(double encode_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.encodes++;
        add_sample(encode_ms, state.window.encode_sum, state.window.encode_max);
        // the value is kept so a slow render line can split this frame's present
        state.window.last_encode = encode_ms;
        report_if_due(state, now);
    }

    void profile_commit(double commit_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.commits++;
        add_sample(commit_ms, state.window.commit_sum, state.window.commit_max);
        // the value is kept so a slow render line can split this frame's present
        state.window.last_commit = commit_ms;
        report_if_due(state, now);
    }

    void profile_drawable_wait(double wait_ms) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.waits++;
        add_sample(wait_ms, state.window.wait_sum, state.window.wait_max);
        // the value is kept so a slow present line can quote this frame's wait
        state.window.last_wait = wait_ms;
        report_if_due(state, now);
    }

    void profile_batch(std::uint64_t vertices, std::uint64_t triangles, std::uint64_t draws, std::uint64_t shadows, std::uint64_t meshes) {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.batch_vertices = vertices;
        state.window.batch_triangles = triangles;
        state.window.batch_draws = draws;
        state.window.batch_shadows = shadows;
        state.window.batch_meshes = meshes;
        report_if_due(state, now);
    }

    void profile_coalesced() {
        ProfileState& state = profile_state();
        // a disabled profile records nothing, which is the whole cost a normal run pays
        if (!state.enabled)
            return;
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.window.coalesced++;
    }
} // namespace albor
