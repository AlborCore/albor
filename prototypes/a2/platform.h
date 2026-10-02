#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace albor::a2
{

    // the opaque platform state, defined by the implementation file so that no platform type leaks (§3.5)
    struct PlatformWindowState;

    // the window metrics the framework treats as authoritative (§9.1)
    struct Metrics
    {
        std::size_t logical_width = 0;
        std::size_t logical_height = 0;
        std::size_t pixel_width = 0;
        std::size_t pixel_height = 0;
        double scale_factor = 1.0;
        double refresh_hz = 60.0;
    };

    // one presented frame, with the timings the platform slice has to report (§28.2)
    struct FrameRecord
    {
        std::uint64_t index = 0;
        double interval_ms = 0.0; // time since the previous presented frame
        double input_to_present_ms = 0.0; // time since the resize request that triggered it, zero when idle
        std::size_t logical_width = 0;
        std::size_t logical_height = 0;
        std::size_t pixel_width = 0;
        std::size_t pixel_height = 0;
        double scale_factor = 1.0;
        bool resizing = false;
    };

    // the slice of the platform layer this prototype needs, without any platform type in the interface (§3.5, AD-05)
    class PlatformWindow
    {
    public:
        PlatformWindow();
        ~PlatformWindow();

        PlatformWindow(const PlatformWindow&) = delete;
        PlatformWindow& operator=(const PlatformWindow&) = delete;
        PlatformWindow(PlatformWindow&&) = delete;
        PlatformWindow& operator=(PlatformWindow&&) = delete;

        // create the native window with its metal layer
        [[nodiscard]] bool create(const std::string& title, std::size_t logical_width, std::size_t logical_height);

        // show the window and let the application enter its run loop
        void show();

        // run the loop until the window closes, handing every presented frame to the callback
        void run(const std::function<void(const FrameRecord&)>& on_frame);

        // request a resize, which is the platform's live resize entry point (§7.4)
        void request_resize(std::size_t logical_width, std::size_t logical_height);

        // run a nested native modal loop for the given time, which is the re-entrancy case of §7.4
        void run_nested_loop_for_ms(std::size_t duration_ms);

        // ask the window to close after the given number of presented frames
        void close_after_frames(std::size_t frames);

        [[nodiscard]] Metrics metrics() const;

        [[nodiscard]] std::size_t frames_presented() const;
        [[nodiscard]] std::size_t resize_events() const;
        [[nodiscard]] std::size_t scale_changes() const;

        // frames that were presented while a resize was in flight, which is the live resize answer of §7.4
        [[nodiscard]] std::size_t frames_presented_while_resizing() const;

        // posted tasks that ran inside the nested loop, which is the event source answer of §7.4
        [[nodiscard]] std::size_t posted_tasks_during_nested_loop() const;

        // posted tasks that ran in the steady state, as the baseline for the nested loop count
        [[nodiscard]] std::size_t posted_tasks_total() const;

    private:
        PlatformWindowState* m_state = nullptr;
    };

} // namespace albor::a2
