#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "prototypes/a1/render_executor.h"
#include "prototypes/a2/platform.h"

namespace
{

    // the workload and the thresholds that were recorded before the run (prototypes/a1/plan.md)
    constexpr std::size_t STEADY_FRAMES = 400;
    constexpr std::size_t RESIZE_STEPS = 60;
    constexpr std::size_t WINDOW_START_WIDTH = 320;
    constexpr std::size_t WINDOW_START_HEIGHT = 240;
    constexpr std::size_t WINDOW_END_WIDTH = 1280;
    constexpr std::size_t WINDOW_END_HEIGHT = 720;
    constexpr std::size_t NESTED_LOOP_MS = 500;
    constexpr std::size_t VERTEX_COUNT = 4096;
    constexpr std::size_t TOTAL_FRAMES = STEADY_FRAMES + RESIZE_STEPS + 120;
    constexpr std::size_t DEADLINE_MS = 40000;

    constexpr double FRAME_INTERVAL_BUDGET_MS = 16.7;
    constexpr double SUBMIT_BLOCKED_BUDGET_MS = 1.0;
    constexpr double BUILD_BUDGET_MS = 2.0;
    constexpr double SUBMIT_TO_PRESENT_BUDGET_MS = 25.0;
    constexpr double RESIZE_PRESENT_RATIO = 0.95;

    // the clock both stages measure with
    using Clock = std::chrono::steady_clock;

    // convert a duration into milliseconds
    double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

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

int main() {
    using namespace albor::a1;

    // report the prototype and the phase that runs
    std::printf("Albor Prototype A1.2a: ui thread presents, worker records (§10.2, AD-18 R1)\n");
    std::fflush(stdout);

    // create the platform window that the render thread presents into
    albor::a2::PlatformWindow window;
    if (!window.create("Albor A1 two-stage", WINDOW_START_WIDTH, WINDOW_START_HEIGHT)) {
        std::printf("a1: could not create the platform window, is a window server available?\n");
        return 1;
    }

    // stage 2 must not present on the ui thread, which is the change this phase measures
    window.set_present_on_ui_thread(false);

    // start the render thread that owns presentation
    RenderExecutor executor;
    if (!executor.start(window)) {
        std::printf("a1: could not start the render executor\n");
        return 1;
    }

    // collect the ui thread timings while the run proceeds
    std::vector<double> build_ms;
    std::vector<double> submit_blocked_ms;
    build_ms.reserve(TOTAL_FRAMES);
    submit_blocked_ms.reserve(TOTAL_FRAMES);

    // the state the driver advances from one displayed frame to the next
    std::size_t steady_seen = 0;
    std::size_t resize_steps_done = 0;
    bool nested_loop_done = false;
    bool deadline_reached = false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(DEADLINE_MS);

    // close the window once the whole workload has been driven
    window.close_after_frames(TOTAL_FRAMES);

    // put the window on screen and start the frame clock
    window.show();

    // stage 1 runs on the ui thread: build the immutable scene and hand it to stage 2 (§10.2)
    window.run([&](const albor::a2::FrameRecord& frame) {
        // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
        if (Clock::now() > deadline) {
            deadline_reached = true;
            window.close_after_frames(1);
            return;
        }

        // build the scene for this frame
        const Clock::time_point build_start = Clock::now();
        FrameScene scene;
        scene.index = frame.index;
        scene.metrics = window.metrics();
        scene.vertices.resize(VERTEX_COUNT);
        for (std::size_t index = 0; index < VERTEX_COUNT; index += 2) {
            scene.vertices.at(index) = static_cast<float>((frame.index + index) % 1024) / 1024.0F;
            scene.vertices.at(index + 1) = static_cast<float>((frame.index * 2 + index) % 512) / 512.0F;
        }
        const double built = to_ms(Clock::now() - build_start);
        executor.set_build_ms(built);

        // hand the immutable scene to the render thread and measure the block
        const double blocked = executor.submit(std::move(scene));

        // record the ui thread side of the frame
        build_ms.push_back(built);
        submit_blocked_ms.push_back(blocked);

        // count the steady phase until the resize starts
        if (resize_steps_done == 0) {
            steady_seen++;

            // start the live resize once the steady phase has produced its frames
            if (steady_seen >= STEADY_FRAMES) {
                resize_steps_done = 1;
            }
            return;
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

    // let the render thread drain what it still holds
    executor.stop();

    // collect the timings the render thread recorded
    const std::vector<FrameTiming> timings = executor.timings();

    // split the render side timings into the metrics the phase has to report
    std::vector<double> present_intervals;
    std::vector<double> present_ms;
    std::vector<double> submit_to_present_ms;
    for (const FrameTiming& timing : timings) {
        // ignore the first frame because it has no interval to compare against
        if (timing.interval_ms > 0.0) {
            present_intervals.push_back(timing.interval_ms);
        }
        present_ms.push_back(timing.present_ms);
        submit_to_present_ms.push_back(timing.submit_to_present_ms);
    }

    // report the run
    std::printf("frames submitted          %zu\n", build_ms.size());
    std::printf("frames executed           %zu\n", executor.frames_executed());
    std::printf("frames dropped            %zu\n", executor.frames_dropped());
    std::printf("frames coalesced          %zu\n", executor.frames_coalesced());
    std::printf("resize frames presented   %zu of %zu steps\n", window.frames_presented_while_resizing(), RESIZE_STEPS);
    std::printf("deadline reached          %s\n", deadline_reached ? "yes" : "no");

    // report the ui thread side of the pipeline
    std::printf("scene build p50 / p99 ms  %.4f / %.4f (budget %.1f)\n", percentile(build_ms, 0.50), percentile(build_ms, 0.99), BUILD_BUDGET_MS);
    std::printf("submit blocked p50/p99 ms %.4f / %.4f (budget %.1f)\n", percentile(submit_blocked_ms, 0.50), percentile(submit_blocked_ms, 0.99), SUBMIT_BLOCKED_BUDGET_MS);

    // report the render thread side of the pipeline
    std::printf("present interval p50 ms   %.4f\n", percentile(present_intervals, 0.50));
    std::printf("present interval p99 ms   %.4f (budget %.1f)\n", percentile(present_intervals, 0.99), FRAME_INTERVAL_BUDGET_MS);
    std::printf("present cost p99 ms       %.4f\n", percentile(present_ms, 0.99));
    std::printf("submit to present p99 ms  %.4f (budget %.1f)\n", percentile(submit_to_present_ms, 0.99), SUBMIT_TO_PRESENT_BUDGET_MS);

    // compare every measurement with the thresholds recorded before the run
    const bool interval_ok = percentile(present_intervals, 0.99) <= FRAME_INTERVAL_BUDGET_MS;
    const bool blocked_ok = percentile(submit_blocked_ms, 0.99) <= SUBMIT_BLOCKED_BUDGET_MS;
    const bool build_ok = percentile(build_ms, 0.99) <= BUILD_BUDGET_MS;
    const bool latency_ok = percentile(submit_to_present_ms, 0.99) <= SUBMIT_TO_PRESENT_BUDGET_MS;
    const bool resize_ok = static_cast<double>(window.frames_presented_while_resizing()) / static_cast<double>(RESIZE_STEPS) >= RESIZE_PRESENT_RATIO;

    // report whether the phase met its recorded thresholds
    std::printf("interval within budget    %s\n", interval_ok ? "yes" : "no");
    std::printf("blocked within budget     %s\n", blocked_ok ? "yes" : "no");
    std::printf("build within budget       %s\n", build_ok ? "yes" : "no");
    std::printf("latency within budget     %s\n", latency_ok ? "yes" : "no");
    std::printf("resize within budget      %s\n", resize_ok ? "yes" : "no");

    // report a failing run to the shell so a ci job notices
    return interval_ok && blocked_ok && build_ok && latency_ok && resize_ok && !deadline_reached ? 0 : 1;
}
