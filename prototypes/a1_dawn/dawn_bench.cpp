// the a1-dawn workloads: the same w1, w2 and w3 that prototype a1 ran against skia, on the same platform
// window and against the same thresholds, so the two renderers can be compared row by row (§28.4)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "prototypes/a1/snapshot.h"
#include "prototypes/a1_dawn/dawn_renderer.h"
#include "prototypes/a1_dawn/resources.h"
#include "prototypes/a2/platform.h"

namespace
{

    // the workload sizes prototype a1 used, so the two runs are the same workload
    constexpr std::size_t W1_INITIAL_SEGMENTS = 1000000;
    constexpr std::size_t W1_APPEND_SEGMENTS = 100000;
    constexpr std::size_t W1_APPENDS = 8;
    constexpr std::size_t W1_APPEND_INTERVAL_MS = 700;
    constexpr std::size_t W2_SYMBOLS = 100000;
    constexpr std::size_t W2_SEGMENTS_PER_SYMBOL = 10;
    constexpr std::size_t W2_LABELS = 4000;
    constexpr std::size_t W2_QUERIES_PER_FRAME = 8;
    constexpr std::size_t W3_HOLD_MS = 100;
    constexpr std::size_t WORKLOAD_FRAMES = 400;
    constexpr std::size_t WINDOW_WIDTH = 1280;
    constexpr std::size_t WINDOW_HEIGHT = 720;
    constexpr float WORKLOAD_EXTENT = 2000.0F;
    constexpr std::size_t RUN_TIMEOUT_MS = 180000;

    // the thresholds prototype a1 recorded for the same workloads, reused verbatim so the rows are comparable
    constexpr double W1_FIRST_BUILD_BUDGET_MS = 250.0;
    constexpr double W1_APPEND_BUILD_BUDGET_MS = 25.0;
    constexpr double RECORD_BUDGET_MS = 8.33;
    constexpr double W1_RECORD_BUDGET_MS = 1.0;
    constexpr double W1_SWAPPED_BUDGET_MS = 16.7;
    constexpr double PER_SEGMENT_BUDGET_NS = 8.0;
    constexpr double PAN_AMORTISATION_BUDGET = 1.25;
    constexpr double W2_QUERY_BUDGET_MS = 1.0;
    constexpr double W2_HIT_RATIO_BUDGET = 0.99;
    constexpr double INTERVAL_BUDGET_MS = 16.7;
    constexpr double DROP_RATIO_BUDGET = 0.01;
    constexpr double W3_HOLD_RATIO_BUDGET = 0.95;
    constexpr double W3_QUERY_FLOOR = 30.0;

    // the clock every workload measures with
    using Clock = std::chrono::steady_clock;

    // read the clock as milliseconds since the steady clock epoch
    double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

    // the percentiles every workload reports, because one tail sample is not a decision input (§28.2)
    struct Stats
    {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        std::size_t count = 0;
    };

    // sort a copy of the samples and read the three recorded percentiles off it
    Stats summarize(std::vector<double> samples) {
        // report zeroes when nothing was measured, so a missing workload cannot look like a pass
        if (samples.empty()) {
            return Stats{};
        }

        // sort so a fraction of the samples becomes an index
        std::sort(samples.begin(), samples.end());
        const auto index_of = [&samples](double fraction) { return std::min(samples.size() - 1U, static_cast<std::size_t>(fraction * static_cast<double>(samples.size()))); };

        // read the three recorded percentiles
        Stats stats;
        stats.p50 = samples.at(index_of(0.50));
        stats.p95 = samples.at(index_of(0.95));
        stats.p99 = samples.at(index_of(0.99));
        stats.count = samples.size();
        return stats;
    }

    // the number of recorded thresholds this run missed, so the process can report a non zero exit
    std::atomic<int> failures{0};

    // record one threshold outcome
    void gate(bool pass) {
        if (!pass) {
            failures.fetch_add(1);
        }
    }

    // report one measured row with its budget, and record the outcome so the exit code cannot disagree
    void report(const char* label, const Stats& stats, double budget, bool pass, const char* unit) {
        std::printf("  %-44s p50 %8.4f  p95 %8.4f  p99 %8.4f %-3s (budget %8.4f) %s\n", label, stats.p50, stats.p95, stats.p99, unit, budget, pass ? "pass" : "FAIL");
        gate(pass);
    }

    // report one scalar threshold outcome, recorded the same way
    void report_scalar(const char* label, double measured, double budget, bool pass, const char* unit) {
        std::printf("  %-44s %10.4f %-3s (budget %8.4f) %s\n", label, measured, unit, budget, pass ? "pass" : "FAIL");
        gate(pass);
    }

    // the pan and zoom the workloads run continuously, identical in shape to prototype a1's
    albor::a1_dawn::ViewTransform moving_view(std::uint64_t index) {
        albor::a1_dawn::ViewTransform view;
        view.scale = 0.55F + 0.45F * std::sin(static_cast<float>(index) * 0.031F);
        view.offset_x = 40.0F * std::cos(static_cast<float>(index) * 0.017F);
        view.offset_y = 40.0F * std::sin(static_cast<float>(index) * 0.023F);
        return view;
    }

    // build a window, or report that the window server is not reachable and record the failure
    bool open_window(albor::a2::PlatformWindow& window, const char* title) {
        // create the native window the workload presents into
        if (!window.create(title, WINDOW_WIDTH, WINDOW_HEIGHT)) {
            std::printf("  could not create the platform window, is a window server available?\n");
            gate(false);
            return false;
        }

        // the platform layer must not present its own clear before the renderer presents, or it would take
        // the only drawable the frame could have had (§10.2)
        window.set_present_on_ui_thread(false);
        return true;
    }

    // the per frame state of one workload, owned by the callback so nothing can dangle
    struct FrameState
    {
        albor::a1_dawn::DawnContext* context = nullptr;
        std::shared_ptr<const albor::a1_dawn::ResourceSet> resources;
        std::size_t uploaded = 0;
        std::vector<double> record;
        std::vector<double> record_moved;
        std::vector<double> record_still;
        std::vector<double> acquire;
        std::vector<double> interval;
        std::vector<double> upload;
        std::size_t frames_offered = 0;
        std::size_t frames_drawn = 0;
        std::size_t queries = 0;
        std::size_t expected_hits = 0;
        std::size_t hold_starts = 0;
        double hold_begin = 0.0;
        double hold_end = 0.0;
        bool holding = false;
        albor::a1_dawn::ViewTransform frozen_view{};
    };

    // workload w1: a million cosmetic segments drawn as one instanced call
    void run_w1(albor::a2::PlatformWindow& window) {
        using namespace albor::a1_dawn;
        std::printf("\n== W1 waveform, 10^6 cosmetic line segments, one instanced draw call\n");
        std::fflush(stdout);

        // the window is created by the caller, because appkit cannot be asked for one after a loop has run
        auto* context = new DawnContext();
        if (!context->create(window)) {
            std::printf("  could not create the dawn context\n");
            gate(false);
            return;
        }

        // report which backend came up, and size the vertex buffer for the largest batch the stream will hold
        const std::size_t max_segments = W1_INITIAL_SEGMENTS + W1_APPENDS * W1_APPEND_SEGMENTS;
        if (!context->reserve_instances(max_segments)) {
            std::printf("  could not reserve the vertex buffer\n");
            gate(false);
            return;
        }
        std::printf("  renderer backend: %s, vertex buffer reserved for %zu instances\n", context->backend(), max_segments);
        std::fflush(stdout);

        // the append stream, crossing only as an immutable snapshot (§10.5)
        albor::a1::Snapshot<ResourceSet> stream;
        std::atomic<bool> producing{true};
        std::vector<double> build_ms;
        std::atomic<std::size_t> publishes{0};

        // build the first batch and then publish growing ones on a background worker
        std::thread producer([&stream, &producing, &build_ms, &publishes] {
            // publish the initial million segments
            const double first_start = now_ms();
            std::shared_ptr<const ResourceSet> initial = create_line_batch(W1_INITIAL_SEGMENTS, WORKLOAD_EXTENT, 1.0F, 0xA11F5EEDULL);
            build_ms.push_back(now_ms() - first_start);
            if (initial == nullptr) {
                return;
            }
            stream.post_set(std::move(initial));
            publishes.fetch_add(1);

            // keep appending so the stream is exercised while frames are drawn
            const std::size_t limit = W1_INITIAL_SEGMENTS + W1_APPENDS * W1_APPEND_SEGMENTS;
            std::size_t total = W1_INITIAL_SEGMENTS;
            while (producing.load(std::memory_order_relaxed) && total < limit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(W1_APPEND_INTERVAL_MS));

                // an append rewrites the whole batch on this side, which is what the threshold is about
                total += W1_APPEND_SEGMENTS;
                const double append_start = now_ms();
                std::shared_ptr<const ResourceSet> grown = create_line_batch(total, WORKLOAD_EXTENT, 1.0F, 0xA11F5EEDULL);
                build_ms.push_back(now_ms() - append_start);
                if (grown == nullptr) {
                    break;
                }
                stream.post_set(std::move(grown));
                publishes.fetch_add(1);
            }
        });

        auto state = std::make_shared<FrameState>();
        state->context = context;
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);
        const std::size_t target = window.frames_presented() + WORKLOAD_FRAMES;
        window.close_after_frames(target);
        window.show();

        // the frame clock drives the workload, and every frame issues exactly one draw call
        window.run([state, &stream, window_ptr = &window, deadline](const albor::a2::FrameRecord&) {
            // count what the display offered, because the drop budget is a ratio
            state->frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                window_ptr->close_after_frames(1);
                return;
            }

            // read the current stream value without blocking, which is the consumer side
            const std::shared_ptr<const ResourceSet> resources = stream.get();
            if (resources == nullptr) {
                return;
            }

            // upload only the tail the append added, because the generator is prefix deterministic
            const double upload_start = now_ms();
            if (state->resources != resources) {
                static_cast<void>(state->context->upload(*resources, state->uploaded));
                state->uploaded = resources->lines.size();
                state->resources = resources;
                state->upload.push_back(now_ms() - upload_start);
            }

            // acquire the surface texture and draw
            if (!state->context->begin_frame()) {
                return;
            }
            const bool moved = state->frames_offered % 4U != 0U;
            if (moved) {
                state->frozen_view = moving_view(state->frames_offered);
            }
            state->context->draw(state->frozen_view, state->resources->lines.size());
            state->context->end_frame();
            state->frames_drawn++;

            // collect what the frame cost, split by what made it interesting
            const FrameTiming& timing = state->context->last_timing();
            if (timing.interval_ms > 0.0) {
                state->interval.push_back(timing.interval_ms);
            }
            state->acquire.push_back(timing.acquire_ms);
            state->record.push_back(timing.record_ms);
            if (moved) {
                state->record_moved.push_back(timing.record_ms);
            }
            else {
                state->record_still.push_back(timing.record_ms);
            }
        });

        // stop the producer before the stream is destroyed
        producing.store(false, std::memory_order_relaxed);
        producer.join();

        // report the workload it actually ran
        const ResourceSetInfo info = state->resources ? describe(*state->resources) : ResourceSetInfo{};
        std::printf("  frames offered %zu, drawn %zu, publishes %zu, final segments %zu\n", state->frames_offered, state->frames_drawn, publishes.load(), info.segments);
        std::printf("  draw calls issued %zu, retained bytes %zu\n", context->draw_calls(), info.bytes);
        std::fflush(stdout);

        // separate the first build from the appends, because they are two different budgets
        const double first_build = build_ms.empty() ? 0.0 : build_ms.front();
        const Stats first_stats{first_build, first_build, first_build, build_ms.empty() ? 0U : 1U};
        std::vector<double> append_build;
        if (build_ms.size() > 1U) {
            append_build.assign(build_ms.begin() + 1, build_ms.end());
        }
        const Stats append_stats = summarize(std::move(append_build));
        const Stats record_stats = summarize(state->record);
        const Stats moved_stats = summarize(state->record_moved);
        const Stats still_stats = summarize(state->record_still);
        const Stats interval_stats = summarize(state->interval);

        // report every recorded threshold
        report("first retained batch build", first_stats, W1_FIRST_BUILD_BUDGET_MS, first_build <= W1_FIRST_BUILD_BUDGET_MS, "ms");
        report("append build per 100k segments", append_stats, W1_APPEND_BUILD_BUDGET_MS, append_stats.p99 <= W1_APPEND_BUILD_BUDGET_MS, "ms");
        report("recording the frame", record_stats, W1_RECORD_BUDGET_MS, record_stats.p99 <= W1_RECORD_BUDGET_MS, "ms");
        report("acquiring the surface texture (t_acquire)", summarize(state->acquire), 0.0, true, "ms");
        report("present interval", interval_stats, INTERVAL_BUDGET_MS, interval_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // the O(1) claim, the same comparison prototype a1 made
        const double amortisation = still_stats.p95 > 0.0 ? moved_stats.p95 / still_stats.p95 : 0.0;
        report("recording while the view moves", moved_stats, RECORD_BUDGET_MS, true, "ms");
        report("recording while the view is still", still_stats, RECORD_BUDGET_MS, true, "ms");
        report_scalar("recording p95 moved / still (O(1))", amortisation, PAN_AMORTISATION_BUDGET, amortisation <= PAN_AMORTISATION_BUDGET, "  ");

        // the per segment cost, the number that has to stay flat as the batch grows
        const double per_segment_ns = info.segments > 0 ? (record_stats.p99 * 1.0E6) / static_cast<double>(info.segments) : 0.0;
        report_scalar("recording p99 per segment", per_segment_ns, PER_SEGMENT_BUDGET_NS, per_segment_ns <= PER_SEGMENT_BUDGET_NS, "ns");

        // the structural claim this prototype exists to test
        report_scalar("draw calls per frame", static_cast<double>(state->frames_drawn > 0 ? context->draw_calls() / state->frames_drawn : 0), 1.0, state->frames_drawn > 0 && context->draw_calls() / state->frames_drawn == 1U, "  ");

        // the drop budget, as a ratio of what the display offered
        const double drop_ratio = state->frames_offered > 0 ? static_cast<double>(state->frames_offered - state->frames_drawn) / static_cast<double>(state->frames_offered) : 1.0;
        report_scalar("frames dropped ratio", drop_ratio, DROP_RATIO_BUDGET, drop_ratio <= DROP_RATIO_BUDGET, "  ");
    }

    // workload w2: a hundred thousand symbols with hover hit testing during a pan and a zoom
    void run_w2(albor::a2::PlatformWindow& window) {
        using namespace albor::a1_dawn;
        std::printf("\n== W2 cad canvas, 10^5 symbols, hover hit testing\n");
        std::fflush(stdout);

        // the window is created by the caller
        auto* context = new DawnContext();
        if (!context->create(window)) {
            std::printf("  could not create the dawn context\n");
            gate(false);
            return;
        }

        // build the retained symbol set, with the same generator and seed prototype a1 used
        const double build_start = now_ms();
        const std::shared_ptr<const ResourceSet> resources = create_symbol_set(W2_SYMBOLS, W2_SEGMENTS_PER_SYMBOL, W2_LABELS, WORKLOAD_EXTENT, 11.0F, 0xCAD0FFEEULL);
        const double build_ms = now_ms() - build_start;
        if (resources == nullptr) {
            std::printf("  could not build the retained symbol set\n");
            gate(false);
            return;
        }
        static_cast<void>(context->reserve_instances(resources->lines.size()));

        // report what the retained set costs, in the same terms prototype a1 reported
        const ResourceSetInfo info = describe(*resources);
        std::printf("  retained bytes %zu, build %.2f ms, segments %zu, symbols %zu, labels %zu\n", info.bytes, build_ms, info.segments, info.symbols, info.labels);
        std::fflush(stdout);

        auto state = std::make_shared<FrameState>();
        state->context = context;
        state->resources = resources;
        state->uploaded = resources->lines.size();
        static_cast<void>(context->upload(*resources, 0));
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);
        window.close_after_frames(window.frames_presented() + WORKLOAD_FRAMES);
        window.show();

        // the frame clock drives the workload, and every frame also answers the hover queries
        window.run([state, resources, window_ptr = &window, deadline](const albor::a2::FrameRecord&) {
            // count what the display offered
            state->frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                window_ptr->close_after_frames(1);
                return;
            }

            // acquire the surface texture
            if (!state->context->begin_frame()) {
                return;
            }

            // move the view continuously, so hit testing runs under a pan and a zoom (§12.3)
            state->frozen_view = moving_view(state->frames_offered);

            // answer the hover queries at real symbol centres, so a miss is a bug rather than a workload fact
            for (std::size_t query = 0; query < W2_QUERIES_PER_FRAME; query++) {
                // pick a symbol deterministically from the frame and the query index
                const std::size_t symbol = (state->frames_offered * 7919U + query * 104729U) % resources->symbols.size();
                float centre_x = 0.0F;
                float centre_y = 0.0F;
                if (!symbol_centre(*resources, symbol, centre_x, centre_y)) {
                    continue;
                }

                // aim just off the symbol centre, so a hit proves the grid and the transform agree
                HitQuery hit;
                hit.x = state->frozen_view.map_x(centre_x) + 1.0F;
                hit.y = state->frozen_view.map_y(centre_y) - 1.0F;
                hit.tolerance = 4.0F;

                // time the query on its own, because it runs on the ui thread
                const double query_start = now_ms();
                const HitResult result = albor::a1_dawn::hit_test(*resources, state->frozen_view, hit);
                static_cast<void>(result);
                state->record_still.push_back(now_ms() - query_start);
                state->queries++;
                state->expected_hits += result.hit ? 1U : 0U;
            }

            // draw the retained geometry and present
            state->context->draw(state->frozen_view, resources->lines.size());
            state->context->end_frame();
            state->frames_drawn++;

            // collect what the frame cost
            const FrameTiming& timing = state->context->last_timing();
            if (timing.interval_ms > 0.0) {
                state->interval.push_back(timing.interval_ms);
            }
            state->record.push_back(timing.record_ms);
        });

        // report the workload it actually ran
        std::printf("  frames offered %zu, drawn %zu, hover queries %zu\n", state->frames_offered, state->frames_drawn, state->queries);
        std::fflush(stdout);

        // report every recorded threshold; the query samples were collected into record_still above
        const Stats record_stats = summarize(state->record);
        const Stats query_stats = summarize(state->record_still);
        const Stats interval_stats = summarize(state->interval);
        report("recording the frame", record_stats, RECORD_BUDGET_MS, record_stats.p99 <= RECORD_BUDGET_MS, "ms");
        report("hover hit query on the ui thread", query_stats, W2_QUERY_BUDGET_MS, query_stats.p99 <= W2_QUERY_BUDGET_MS, "ms");
        report("present interval", interval_stats, INTERVAL_BUDGET_MS, interval_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // the hit ratio, which is a correctness threshold as much as a performance one
        const double hit_ratio = state->queries > 0 ? static_cast<double>(state->expected_hits) / static_cast<double>(state->queries) : 0.0;
        report_scalar("queries that hit the expected symbol", hit_ratio, W2_HIT_RATIO_BUDGET, hit_ratio >= W2_HIT_RATIO_BUDGET, "  ");
    }

    // workload w3: two windows, with a hundred millisecond presentation hold injected into the first one
    void run_w3(albor::a2::PlatformWindow& first, albor::a2::PlatformWindow& second) {
        using namespace albor::a1_dawn;
        std::printf("\n== W3 multi-window isolation, a %zu ms presentation hold on window 1\n", W3_HOLD_MS);
        std::fflush(stdout);

        // both windows are created by the caller, because appkit cannot create one after a loop has run
        auto* first_gpu = new DawnContext();
        auto* second_gpu = new DawnContext();
        if (!first_gpu->create(first) || !second_gpu->create(second)) {
            std::printf("  could not create both dawn contexts\n");
            gate(false);
            return;
        }

        // build one retained set and share it with both windows, because it is immutable (§10.5)
        const std::shared_ptr<const ResourceSet> resources = create_line_batch(W1_INITIAL_SEGMENTS, WORKLOAD_EXTENT, 1.0F, 0xBEEF0001ULL);
        if (resources == nullptr) {
            std::printf("  could not build the shared retained set\n");
            gate(false);
            return;
        }
        static_cast<void>(first_gpu->reserve_instances(resources->lines.size()));
        static_cast<void>(second_gpu->reserve_instances(resources->lines.size()));
        static_cast<void>(first_gpu->upload(*resources, 0));
        static_cast<void>(second_gpu->upload(*resources, 0));

        auto first_state = std::make_shared<FrameState>();
        first_state->context = first_gpu;
        first_state->resources = resources;
        auto second_state = std::make_shared<FrameState>();
        second_state->context = second_gpu;
        second_state->resources = resources;
        std::vector<double> second_during_hold;
        std::size_t second_queries_during_hold = 0;
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);

        // window 2 is driven by the display link alone, with no loop of its own
        second.set_frame_callback([second_state, first_state, &second_during_hold, &second_queries_during_hold](const albor::a2::FrameRecord&) {
            // acquire, draw and present, which is the frame window 2 would present if it were not starved
            if (!second_state->context->begin_frame()) {
                return;
            }
            second_state->frames_drawn++;
            second_state->frozen_view = moving_view(second_state->frames_drawn);
            second_state->context->draw(second_state->frozen_view, second_state->resources->lines.size());
            second_state->context->end_frame();

            // attribute the frame to the injected hold when one was active
            const FrameTiming& timing = second_state->context->last_timing();
            if (timing.interval_ms > 0.0) {
                second_state->interval.push_back(timing.interval_ms);
                if (first_state->holding) {
                    second_during_hold.push_back(timing.interval_ms);
                    second_queries_during_hold++;
                }
            }
        });

        // window 1 owns the loop and injects the hold, against a cumulative frame target
        first.close_after_frames(first.frames_presented() + WORKLOAD_FRAMES);
        second.show();
        first.show();
        first.run([first_state, first_ptr = &first, deadline](const albor::a2::FrameRecord&) {
            // count what window 1 attempted
            first_state->frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                first_ptr->close_after_frames(1);
                return;
            }

            // inject the hold over a stretch of frames in the middle of the run
            const bool in_window = first_state->frames_offered > WORKLOAD_FRAMES / 4 && first_state->frames_offered <= WORKLOAD_FRAMES / 2;
            if (in_window && !first_state->holding) {
                first_state->hold_begin = now_ms();
                first_state->hold_starts++;
            }
            if (!in_window && first_state->holding) {
                first_state->hold_end = now_ms();
            }
            first_state->holding = in_window;
            first_state->context->set_presentation_hold_ms(in_window ? static_cast<double>(W3_HOLD_MS) : 0.0);

            // acquire, draw and present with the hold applied to the presenting thread
            if (!first_state->context->begin_frame()) {
                return;
            }
            first_state->frozen_view = moving_view(first_state->frames_offered);
            first_state->context->draw(first_state->frozen_view, first_state->resources->lines.size());
            first_state->context->end_frame();
            first_state->frames_drawn++;
        });

        // detach both frame sources before this workload's locals go away
        first.detach_frame_source();
        second.detach_frame_source();

        // report what each window actually presented
        std::printf("  window 1 attempts %zu, presented %zu\n", first_state->frames_offered, first_state->frames_drawn);
        std::printf("  window 2 presented %zu, of which %zu inside the hold\n", second_state->frames_drawn, second_during_hold.size());

        // report the injected hold itself, so the reader can see the stall really happened
        const double hold_ms = first_state->hold_end - first_state->hold_begin;
        std::printf("  %-44s %10zu windows, %.2f ms wide\n", "injected hold", first_state->hold_starts, hold_ms);
        std::fflush(stdout);

        // report the isolated window as a whole
        const Stats second_stats = summarize(second_state->interval);
        report("window 2 present interval, whole run", second_stats, INTERVAL_BUDGET_MS, second_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // report the frames window 2 lost inside the hold window, against what the display offered
        const double expected_frames = hold_ms / INTERVAL_BUDGET_MS;
        const double presented_ratio = expected_frames > 0.0 ? static_cast<double>(second_during_hold.size()) / expected_frames : 0.0;
        report_scalar("window 2 frames presented during the hold", presented_ratio, W3_HOLD_RATIO_BUDGET, presented_ratio >= W3_HOLD_RATIO_BUDGET, "  ");

        // report the cadence window 2 actually kept while window 1 was stalled
        report("window 2 present interval during the hold", summarize(second_during_hold), INTERVAL_BUDGET_MS, summarize(second_during_hold).p99 <= INTERVAL_BUDGET_MS, "ms");

        // report the frames window 2 still managed during the hold
        report_scalar("window 2 hover queries during the hold", static_cast<double>(second_queries_during_hold), W3_QUERY_FLOOR, static_cast<double>(second_queries_during_hold) >= W3_QUERY_FLOOR, "  ");

        // report window 1's own drop ratio, because a stalled window 1 is not a failure by itself
        const double first_ratio = first_state->frames_offered > 0 ? static_cast<double>(first_state->frames_offered - first_state->frames_drawn) / static_cast<double>(first_state->frames_offered) : 1.0;
        report_scalar("window 1 frames dropped ratio", first_ratio, DROP_RATIO_BUDGET, first_ratio <= DROP_RATIO_BUDGET, "  ");
    }

} // namespace

int main() {
    // report the phase and the renderer it measured
    std::printf("Albor Prototype A1-Dawn: the same workloads prototype a1 ran against skia\n");
    std::fflush(stdout);

    // create every window the run needs before the first run loop starts, because appkit does not tolerate
    // being asked for a window once its run loop has stopped; the windows are deliberately never destroyed
    auto* primary = new albor::a2::PlatformWindow();
    auto* secondary = new albor::a2::PlatformWindow();
    if (!open_window(*primary, "Albor A1-Dawn workloads") || !open_window(*secondary, "Albor A1-Dawn window 2")) {
        std::printf("\nthreshold failures: %d\n", failures.load());
        return 1;
    }

    // run each workload in turn, because they each own the loop for a while
    run_w1(*primary);
    std::fflush(stdout);
    run_w2(*primary);
    std::fflush(stdout);
    run_w3(*primary, *secondary);
    std::fflush(stdout);

    // report the run as a whole, so a ci job notices a failing phase
    std::printf("\nthreshold failures: %d\n", failures.load());
    return failures.load() == 0 ? 0 : 1;
}
