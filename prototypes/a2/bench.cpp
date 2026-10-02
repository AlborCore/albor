#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "prototypes/a2/platform.h"

namespace albor::a2
{

    namespace
    {

        // the workload and the thresholds that were recorded before the run (§28.4)
        constexpr std::size_t STEADY_FRAMES = 200;
        constexpr std::size_t RESIZE_STEPS = 60;
        constexpr std::size_t WINDOW_START_WIDTH = 320;
        constexpr std::size_t WINDOW_START_HEIGHT = 240;
        constexpr std::size_t WINDOW_END_WIDTH = 1280;
        constexpr std::size_t WINDOW_END_HEIGHT = 720;
        constexpr std::size_t NESTED_LOOP_MS = 500;
        constexpr double FRAME_INTERVAL_BUDGET_MS = 16.7;
        constexpr double INPUT_TO_PRESENT_BUDGET_MS = 16.7;
        constexpr double RESIZE_PRESENT_RATIO = 0.95;
        constexpr double NESTED_LOOP_TICKS_PER_MS = 1.0 / FRAME_INTERVAL_BUDGET_MS;

        // the nested modal session is expected to keep the frame source running for its whole duration
        constexpr std::size_t NESTED_LOOP_FRAMES = static_cast<std::size_t>(static_cast<double>(NESTED_LOOP_MS) / FRAME_INTERVAL_BUDGET_MS) + 30;

        // a deadline that stops the run even if the platform never reaches the requested frame count
        constexpr std::size_t DEADLINE_MS = 30000;

        // the total number of frames the workload expects before the window closes
        constexpr std::size_t TOTAL_FRAMES = STEADY_FRAMES + RESIZE_STEPS + NESTED_LOOP_FRAMES + 10;

        // return the sample at the given fraction of a sorted copy of the samples
        double percentile(std::vector<double> samples, double fraction) {
            // report no latency when nothing was measured
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index
            std::ranges::sort(samples);

            // clamp the index so a fraction of one stays inside the samples
            const std::size_t index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));

            // hand back the sample at that position
            return samples.at(index);
        }

    } // namespace

} // namespace albor::a2

int main() {
    using namespace albor::a2;

    // report the prototype and the questions it answers
    std::printf("Albor Prototype A2: platform presentation, macOS slice (§30)\n");
    std::fflush(stdout);

    // create the platform window with a metal layer
    PlatformWindow window;
    if (!window.create("Albor A2 macOS", WINDOW_START_WIDTH, WINDOW_START_HEIGHT)) {
        std::printf("a2: could not create the platform window, is a window server available?\n");
        return 1;
    }

    // collect the frame timings as the platform presents them
    std::vector<double> steady_intervals;
    std::vector<double> resize_input_to_present;
    steady_intervals.reserve(TOTAL_FRAMES);
    resize_input_to_present.reserve(RESIZE_STEPS);

    // the state the driver advances from one presented frame to the next
    std::size_t steady_seen = 0;
    std::size_t resize_steps_done = 0;
    bool nested_loop_done = false;
    bool deadline_reached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(DEADLINE_MS);

    // close the window once the whole workload has been presented
    window.close_after_frames(TOTAL_FRAMES);

    // put the window on screen and start the display link
    window.show();

    // drive the workload from the frame callback, which runs on the platform frame source
    window.run([&](const FrameRecord& frame) {
        // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
        if (std::chrono::steady_clock::now() > deadline) {
            deadline_reached = true;
            window.close_after_frames(1);
            return;
        }

        // measure the steady phase until the resize starts
        if (resize_steps_done == 0) {
            if (frame.interval_ms > 0.0) {
                steady_intervals.push_back(frame.interval_ms);
                steady_seen++;
            }

            // start the live resize once the steady phase has presented its frames
            if (steady_seen >= STEADY_FRAMES) {
                resize_steps_done = 1;
            }
            return;
        }

        // measure the latency of every resize step
        if (frame.input_to_present_ms > 0.0) {
            resize_input_to_present.push_back(frame.input_to_present_ms);
        }

        // request the next step of the live resize while the budget lasts
        if (resize_steps_done <= RESIZE_STEPS) {
            const double progress = static_cast<double>(resize_steps_done) / static_cast<double>(RESIZE_STEPS);
            const std::size_t width = WINDOW_START_WIDTH + static_cast<std::size_t>((WINDOW_END_WIDTH - WINDOW_START_WIDTH) * progress);
            const std::size_t height = WINDOW_START_HEIGHT + static_cast<std::size_t>((WINDOW_END_HEIGHT - WINDOW_START_HEIGHT) * progress);
            window.request_resize(width, height);
            resize_steps_done++;
            return;
        }

        // enter the nested modal session once, after the resize finished
        if (!nested_loop_done) {
            nested_loop_done = true;
            window.run_nested_loop_for_ms(NESTED_LOOP_MS);
        }
    });

    // collect the platform counters after the loop
    const Metrics metrics = window.metrics();

    // report the display the run was measured on
    std::printf("display: %.0f Hz, backing scale %.2f, logical %zux%zu, pixel %zux%zu\n", metrics.refresh_hz, metrics.scale_factor, metrics.logical_width, metrics.logical_height, metrics.pixel_width, metrics.pixel_height);

    // report the pacing result of the steady phase
    const double steady_p99 = percentile(steady_intervals, 0.99);
    std::printf("steady frames            %zu\n", steady_intervals.size());
    std::printf("steady interval p50 ms   %.4f\n", percentile(steady_intervals, 0.50));
    std::printf("steady interval p99 ms   %.4f (budget %.1f)\n", steady_p99, FRAME_INTERVAL_BUDGET_MS);

    // report the live resize result of §7.4
    std::printf("resize events            %zu\n", window.resize_events());
    std::printf("frames while resizing    %zu of %zu steps\n", window.frames_presented_while_resizing(), RESIZE_STEPS);
    std::printf("resize input to present p99 ms %.4f (budget %.1f)\n", percentile(resize_input_to_present, 0.99), INPUT_TO_PRESENT_BUDGET_MS);

    // report the nested loop result of §7.4
    const std::size_t nested_ticks = window.posted_tasks_during_nested_loop();
    const double nested_expected = static_cast<double>(NESTED_LOOP_MS) * NESTED_LOOP_TICKS_PER_MS;
    std::printf("posted tasks total       %zu\n", window.posted_tasks_total());
    std::printf("posted tasks in nested   %zu (expected at least %.0f over %zu ms)\n", nested_ticks, nested_expected, NESTED_LOOP_MS);

    // report the scale change observation of §9.1
    std::printf("scale change events      %zu\n", window.scale_changes());
    std::printf("deadline reached         %s\n", deadline_reached ? "yes" : "no");

    // compare every measurement with the thresholds that were recorded before the run (§28.4)
    const bool steady_ok = steady_p99 <= FRAME_INTERVAL_BUDGET_MS;
    const bool resize_ok = static_cast<double>(window.frames_presented_while_resizing()) / static_cast<double>(RESIZE_STEPS) >= RESIZE_PRESENT_RATIO;
    const bool latency_ok = percentile(resize_input_to_present, 0.99) <= INPUT_TO_PRESENT_BUDGET_MS;
    const bool nested_ok = static_cast<double>(nested_ticks) >= nested_expected;

    // report whether the slice met its recorded thresholds
    std::printf("steady within budget     %s\n", steady_ok ? "yes" : "no");
    std::printf("resize within budget     %s\n", resize_ok ? "yes" : "no");
    std::printf("latency within budget    %s\n", latency_ok ? "yes" : "no");
    std::printf("nested within budget     %s\n", nested_ok ? "yes" : "no");

    // report a failing run to the shell so a ci job notices
    return steady_ok && resize_ok && latency_ok && nested_ok && !deadline_reached ? 0 : 1;
}
