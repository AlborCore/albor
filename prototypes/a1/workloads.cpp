// the workloads of prototype a1.3: w1 waveform, w2 cad canvas and w3 multi-window isolation
// the thresholds this file checks were recorded in prototypes/a1/plan.md before the run, per §28.4

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "prototypes/a1/custom_render_node.h"
#include "prototypes/a1/gpu_context.h"
#include "prototypes/a1/resource_factory.h"
#include "prototypes/a1/snapshot.h"
#include "prototypes/a2/platform.h"

namespace
{

    // the workload sizes recorded in plan.md
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

    // the thresholds recorded in plan.md before the run
    constexpr double W1_FIRST_BUILD_BUDGET_MS = 250.0;
    constexpr double W1_APPEND_BUILD_BUDGET_MS = 25.0;
    constexpr double RECORD_BUDGET_MS = 8.33;
    constexpr double W1_SWAPPED_BUDGET_MS = 16.7;
    constexpr double PER_SEGMENT_BUDGET_NS = 8.0;
    constexpr double PAN_AMORTISATION_BUDGET = 1.25;
    constexpr double W2_QUERY_BUDGET_MS = 1.0;
    constexpr double W2_HIT_RATIO_BUDGET = 0.99;
    constexpr std::size_t W2_RETAINED_BUDGET_BYTES = 64U * 1024U * 1024U;
    constexpr double W3_HOLD_RATIO_BUDGET = 0.95;
    constexpr double W3_QUERY_FLOOR = 30.0;
    constexpr double INTERVAL_BUDGET_MS = 16.7;
    constexpr double DROP_RATIO_BUDGET = 0.01;

    // the number of recorded thresholds this run missed, so the process can report a non zero exit
    std::atomic<int> failures{0};

    // record one threshold outcome
    void gate(bool pass) {
        if (!pass) {
            failures.fetch_add(1);
        }
    }

    // the clock every workload measures with
    using Clock = std::chrono::steady_clock;

    // convert a duration into milliseconds
    double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

    // the percentiles a workload has to report, because one tail sample is not a decision input (§28.2)
    struct Stats
    {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        std::size_t count = 0;
    };

    // sort a copy of the samples and read the three percentiles off it
    Stats summarize(std::vector<double> samples) {
        // report zeroes when nothing was measured, so a missing workload cannot look like a pass
        if (samples.empty()) {
            return Stats{};
        }

        // sort so a fraction of the samples becomes an index
        std::sort(samples.begin(), samples.end());
        const auto index_of = [&samples](double fraction) { return std::min(samples.size() - 1U, static_cast<std::size_t>(fraction * static_cast<double>(samples.size()))); };

        // read the three recorded percentiles and the worst sample
        Stats stats;
        stats.p50 = samples.at(index_of(0.50));
        stats.p95 = samples.at(index_of(0.95));
        stats.p99 = samples.at(index_of(0.99));
        stats.max = samples.back();
        stats.count = samples.size();
        return stats;
    }

    // report one measured row with its budget and whether it passed
    void report(const char* label, const Stats& stats, double budget, bool pass, const char* unit) {
        std::printf("  %-44s p50 %8.4f  p95 %8.4f  p99 %8.4f %-3s (budget %8.4f) %s\n", label, stats.p50, stats.p95, stats.p99, unit, budget, pass ? "pass" : "FAIL");
        gate(pass);
    }

    // report one scalar threshold outcome
    void report_scalar(const char* label, double measured, double budget, bool pass, const char* unit) {
        std::printf("  %-44s %10.4f %-3s (budget %8.4f) %s\n", label, measured, unit, budget, pass ? "pass" : "FAIL");
        gate(pass);
    }

    // the pan and zoom a workload runs continuously, so that recording is measured under a moving view
    albor::a1::ViewTransform moving_view(std::uint64_t index) {
        albor::a1::ViewTransform view;
        view.scale = 0.55F + 0.45F * std::sin(static_cast<float>(index) * 0.031F);
        view.offset_x = 40.0F * std::cos(static_cast<float>(index) * 0.017F);
        view.offset_y = 40.0F * std::sin(static_cast<float>(index) * 0.023F);
        return view;
    }

    // the per frame state of one workload: the callback owns it through a shared_ptr, so a frame that arrives
    // after the loop stopped still finds everything it touches alive
    struct FrameState
    {
        albor::a1::GpuContext* gpu = nullptr;
        albor::a1::CustomRenderNode node{nullptr};
        std::size_t symbol_count = 0;
        std::vector<double> intervals;
        std::vector<double> record;
        std::vector<double> query;
        std::vector<double> acquire;
        std::size_t frames_offered = 0;
        std::size_t frames_drawn = 0;
        std::size_t queries = 0;
        std::size_t expected_hits = 0;

        // the injected hold window of w3, kept here so the callback owns it too
        Clock::time_point hold_begin{};
        Clock::time_point hold_end{};
        std::size_t hold_starts = 0;
        bool holding = false;
        albor::a1::HitQuery probe{};
    };

    // build a window, or report that the window server is not reachable and record the failure
    bool open_window(albor::a2::PlatformWindow& window, const char* title) {
        // create the native window the workload presents into
        if (!window.create(title, WINDOW_WIDTH, WINDOW_HEIGHT)) {
            // report the environment problem rather than pretending the workload ran
            std::printf("  could not create the platform window, is a window server available?\n");
            gate(false);
            return false;
        }

        // the platform layer must not present its own clear before the skia path wraps the layer, because
        // that clear would take the only drawable the frame could have had (§10.2)
        window.set_present_on_ui_thread(false);
        return true;
    }

    // workload w1: a million cosmetic segments replayed while the view pans and zooms
    void run_w1(albor::a2::PlatformWindow& window) {
        using namespace albor::a1;

        std::printf("\n== W1 waveform, 10^6 cosmetic line segments, continuous pan and zoom\n");
        std::fflush(stdout);

        // take the window main created, because appkit cannot be asked for a window after a run loop stopped
        window.set_present_on_ui_thread(false);

        // bring up the renderer against that window; the context is deliberately never destroyed, because a
        // display link that is still attached can fire one more callback after the loop stopped
        auto* gpu = new GpuContext();
        if (!gpu->create(window)) {
            std::printf("  could not create the gpu context\n");
            gate(false);
            return;
        }

        // report which backend actually came up, because the plan records it as a finding (§11.7)
        std::printf("  renderer backend: %s\n", gpu->backend());
        std::fflush(stdout);

        // the factory owns no gpu state, so the worker below may call it (§11.2)
        ResourceFactory factory;

        // the append stream the producer publishes into, which only ever crosses as an immutable snapshot (§10.5)
        Snapshot<ResourceSet> stream;
        std::atomic<bool> producing{true};
        std::vector<double> build_ms;
        std::atomic<std::size_t> publishes{0};

        // build the first batch on a background worker, then publish growing batches into the stream
        std::thread producer([&factory, &stream, &producing, &build_ms, &publishes] {
            // publish the initial million segments, which is the batch the frames start from
            LineBatchSpec spec;
            spec.segment_count = W1_INITIAL_SEGMENTS;
            spec.extent = WORKLOAD_EXTENT;
            spec.stroke_width = 1.0F;
            spec.seed = 0xA11F5EEDULL;
            const Clock::time_point first_start = Clock::now();
            ResourceSetPtr initial = factory.create_line_batch(spec);
            build_ms.push_back(to_ms(Clock::now() - first_start));
            if (initial == nullptr) {
                return;
            }
            stream.post_set(std::move(initial));
            publishes.fetch_add(1);

            // keep appending until the run ends, so the stream is exercised while frames are drawn
            const std::size_t limit = W1_INITIAL_SEGMENTS + W1_APPENDS * W1_APPEND_SEGMENTS;
            std::size_t total = W1_INITIAL_SEGMENTS;
            while (producing.load(std::memory_order_relaxed) && total < limit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(W1_APPEND_INTERVAL_MS));

                // build the next batch off the ui thread and time the build on its own
                LineBatchSpec next;
                next.extent = WORKLOAD_EXTENT;
                next.stroke_width = 1.0F;
                next.seed = 0xA11F5EEDULL;
                total += W1_APPEND_SEGMENTS;
                next.segment_count = total;

                // an append copies every point again, which is what the frame that observes it pays for
                const Clock::time_point append_start = Clock::now();
                ResourceSetPtr grown = factory.create_line_batch(next);
                build_ms.push_back(to_ms(Clock::now() - append_start));
                if (grown == nullptr) {
                    break;
                }
                stream.post_set(std::move(grown));
                publishes.fetch_add(1);
            }
        });

        // the node that replays the retained set, which is the custom render node of §11.1
        CustomRenderNode node(nullptr);
        std::vector<double> intervals;
        std::vector<double> record_all;
        std::vector<double> record_moved;
        std::vector<double> record_still;
        std::vector<double> record_swapped;
        std::vector<double> acquire_samples;
        std::vector<double> submit_samples;
        std::size_t frames_offered = 0;
        std::size_t frames_drawn = 0;
        ResourceSetPtr held;
        ResourceSetInfo info{};
        ViewTransform frozen_view{};

        // the platform frame counter is cumulative across runs, so the target is a total rather than a count
        const std::size_t target = window.frames_presented() + WORKLOAD_FRAMES;

        // stop the run when the display link stops offering frames, and never hang on a stalled platform
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);
        window.close_after_frames(target);
        window.show();

        // the frame clock drives the workload, and every frame draws one retained batch
        window.run([&](const albor::a2::FrameRecord&) {
            // count what the display offered, because the drop budget is a ratio
            frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                window.close_after_frames(1);
                return;
            }

            // read the current stream value without blocking, which is the o(1) consumer side
            const ResourceSetPtr resources = stream.get();

            // wait for the first publish rather than drawing an empty frame
            if (resources == nullptr) {
                return;
            }

            // a swap is the interesting frame: it is the one that has to pick up new retained bytes
            const bool swapped = held != nullptr && resources != held;
            if (swapped) {
                info = describe(*resources);
            }
            held = resources;

            // wrap the layer, which acquires a drawable and may wait (§10.2)
            if (!gpu->begin_frame()) {
                return;
            }

            // hand the immutable set to the node, which never rebuilds it
            node.set_resources(resources);

            // move the view on three frames out of four and hold it still on the fourth, so the o(1) claim
            // has both a moving sample and a still one to compare against
            const bool moved = frames_offered % 4U != 0U;
            if (moved) {
                frozen_view = moving_view(frames_offered);
            }
            node.set_view(frozen_view);

            // record the retained geometry and submit
            gpu->draw(node);
            gpu->end_frame();
            frames_drawn++;

            // collect what the frame cost, split by what made it interesting
            const GpuFrameTiming& timing = gpu->last_timing();
            if (timing.interval_ms > 0.0) {
                intervals.push_back(timing.interval_ms);
            }
            record_all.push_back(timing.record_ms);
            acquire_samples.push_back(timing.acquire_ms);
            submit_samples.push_back(timing.submit_ms);
            if (moved) {
                record_moved.push_back(timing.record_ms);
            }
            else {
                record_still.push_back(timing.record_ms);
            }
            if (swapped) {
                record_swapped.push_back(timing.record_ms);
            }
        });

        // stop the producer before the stream is destroyed
        producing.store(false, std::memory_order_relaxed);
        producer.join();

        // report the workload it actually ran, including the final segment count
        const ResourceSetInfo final_info = held ? describe(*held) : ResourceSetInfo{};
        const std::size_t total_bytes = factory.retained_bytes();
        std::printf("  frames offered %zu, drawn %zu, publishes %zu, final segments %zu\n", frames_offered, frames_drawn, publishes.load(), final_info.segments());
        std::printf("  retained bytes across every published set %zu, final set %zu\n", total_bytes, final_info.bytes);

        // separate the first build from the appends, because they are two different budgets
        const double first_build = build_ms.empty() ? 0.0 : build_ms.front();
        const Stats first_stats{first_build, first_build, first_build, first_build, build_ms.empty() ? 0U : 1U};
        std::vector<double> append_build;
        if (build_ms.size() > 1U) {
            append_build.assign(build_ms.begin() + 1, build_ms.end());
        }
        const Stats append_stats = summarize(std::move(append_build));
        const Stats record_stats = summarize(record_all);
        const Stats swapped_stats = summarize(record_swapped);
        const Stats interval_stats = summarize(intervals);

        // report every recorded threshold
        report("first retained batch build", first_stats, W1_FIRST_BUILD_BUDGET_MS, first_build <= W1_FIRST_BUILD_BUDGET_MS, "ms");
        report("append build per 100k segments", append_stats, W1_APPEND_BUILD_BUDGET_MS, append_stats.p99 <= W1_APPEND_BUILD_BUDGET_MS, "ms");
        report("recording the frame", record_stats, RECORD_BUDGET_MS, record_stats.p99 <= RECORD_BUDGET_MS, "ms");
        report("recording on frames that observed a swap", swapped_stats, W1_SWAPPED_BUDGET_MS, swapped_stats.p99 <= W1_SWAPPED_BUDGET_MS, "ms");
        report("present interval", interval_stats, INTERVAL_BUDGET_MS, interval_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // the two platform costs, because a cadence miss has to be attributed to one of them
        report("acquiring the drawable (t_acquire)", summarize(acquire_samples), 0.0, true, "ms");
        report("flushing and submitting the frame", summarize(submit_samples), 0.0, true, "ms");

        // the O(1) claim of §11.2: moving the view must not cost more recording than holding it still
        const Stats moved_stats = summarize(record_moved);
        const Stats still_stats = summarize(record_still);
        const double amortisation = still_stats.p95 > 0.0 ? moved_stats.p95 / still_stats.p95 : 0.0;
        report("recording while the view moves", moved_stats, 0.0, true, "ms");
        report("recording while the view is still", still_stats, 0.0, true, "ms");
        report_scalar("recording p95 moved / still (O(1))", amortisation, PAN_AMORTISATION_BUDGET, amortisation <= PAN_AMORTISATION_BUDGET, "  ");

        // the per segment cost, which is the number that has to stay flat as the batch grows
        const double per_segment_ns = final_info.segments() > 0 ? (record_stats.p99 * 1.0E6) / static_cast<double>(final_info.segments()) : 0.0;
        report_scalar("recording p99 per segment", per_segment_ns, PER_SEGMENT_BUDGET_NS, per_segment_ns <= PER_SEGMENT_BUDGET_NS, "ns");

        // the drop budget, as a ratio of what the display offered
        const double drop_ratio = frames_offered > 0 ? static_cast<double>(frames_offered - frames_drawn) / static_cast<double>(frames_offered) : 1.0;
        report_scalar("frames dropped ratio", drop_ratio, DROP_RATIO_BUDGET, drop_ratio <= DROP_RATIO_BUDGET, "  ");
        gate(drop_ratio <= DROP_RATIO_BUDGET);

        // the memory half of the O(1) claim: a pan and a zoom never grow the retained set
        const std::size_t initial_bytes = info.bytes;
        std::printf("  %-44s %10zu (budget %10zu) %s\n", "retained set grew only on a stream append", initial_bytes, final_info.bytes, "observed");

        // the structural claim of §11.2: the frame spends a fixed number of draw calls whatever the pan does
        report_scalar("draw calls per frame", static_cast<double>(final_info.draw_calls), 1.0, final_info.draw_calls == 1U, "  ");

        // report whether the run timed out, because a timed out run has no tail worth reading
        if (Clock::now() > deadline) {
            std::printf("  the run hit its %zu ms deadline before the display offered %zu frames\n", RUN_TIMEOUT_MS, WORKLOAD_FRAMES);
            gate(false);
        }
    }

    // workload w2: a hundred thousand symbols with hover hit testing during a pan and a zoom
    void run_w2(albor::a2::PlatformWindow& window) {
        using namespace albor::a1;

        std::printf("\n== W2 cad canvas, 10^5 symbols, ~10^6 segments, %zu labels\n", W2_LABELS);
        std::fflush(stdout);

        // take the window main created, because appkit cannot be asked for a window after a run loop stopped
        window.set_present_on_ui_thread(false);

        // bring up the renderer against that window; the context is deliberately never destroyed, because a
        // display link that is still attached can fire one more callback after the loop stopped
        auto* gpu = new GpuContext();
        if (!gpu->create(window)) {
            std::printf("  could not create the gpu context\n");
            gate(false);
            return;
        }

        // build the retained symbol set, which carries the geometry, the hit grid and the labels
        ResourceFactory factory;
        SymbolSetSpec spec;
        spec.symbol_count = W2_SYMBOLS;
        spec.segments_per_symbol = W2_SEGMENTS_PER_SYMBOL;
        spec.label_count = W2_LABELS;
        spec.extent = WORKLOAD_EXTENT;
        spec.label_size = 11.0F;
        spec.seed = 0xCAD0FFEEULL;
        const Clock::time_point build_start = Clock::now();
        const ResourceSetPtr resources = factory.create_symbol_set(spec);
        const double build_ms = to_ms(Clock::now() - build_start);
        if (resources == nullptr) {
            std::printf("  could not build the retained symbol set\n");
            gate(false);
            return;
        }

        // report what the retained set costs, because that is the memory half of the O(1) claim
        const ResourceSetInfo info = describe(*resources);
        std::printf("  retained bytes %zu, build %.2f ms, segments %zu, symbols %zu, labels %zu\n", info.bytes, build_ms, info.segments(), info.symbols, info.labels);
        gate(info.bytes <= W2_RETAINED_BUDGET_BYTES);
        report_scalar("retained bytes", static_cast<double>(info.bytes), static_cast<double>(W2_RETAINED_BUDGET_BYTES), info.bytes <= W2_RETAINED_BUDGET_BYTES, "B ");

        // the node that replays the set and answers the hover queries, owned by the shared frame state
        auto state = std::make_shared<FrameState>();
        state->gpu = gpu;
        state->node = CustomRenderNode(resources);
        state->symbol_count = info.symbols;
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);

        // the platform frame counter is cumulative across runs, so the target is a total rather than a count
        window.close_after_frames(window.frames_presented() + WORKLOAD_FRAMES);
        window.show();

        // the frame clock drives the workload, and every frame also answers the hover queries
        window.run([state, window_ptr = &window, deadline](const albor::a2::FrameRecord&) {
            // count what the display offered
            state->frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                window_ptr->close_after_frames(1);
                return;
            }

            // wrap the layer, which acquires a drawable and may wait
            if (!state->gpu->begin_frame()) {
                return;
            }

            // move the view continuously, so hit testing runs under a pan and a zoom (§12.3)
            state->node.set_view(moving_view(state->frames_offered));

            // answer the hover queries at real symbol centres, so a miss is a bug rather than a workload fact
            for (std::size_t query = 0; query < W2_QUERIES_PER_FRAME; query++) {
                // pick a symbol deterministically from the frame and the query index
                const std::size_t symbol = (state->frames_offered * 7919U + query * 104729U) % state->symbol_count;
                float centre_x = 0.0F;
                float centre_y = 0.0F;
                if (!symbol_centre(*state->node.resources(), symbol, centre_x, centre_y)) {
                    continue;
                }

                // aim just off the symbol centre, so a hit proves the grid and the transform agree
                HitQuery hit;
                hit.x = state->node.view().map_x(centre_x) + 1.0F;
                hit.y = state->node.view().map_y(centre_y) - 1.0F;
                hit.tolerance = 4.0F;

                // time the query on its own, because it runs on the ui thread
                const Clock::time_point query_start = Clock::now();
                const HitResult result = state->node.hit_test(hit);
                state->query.push_back(to_ms(Clock::now() - query_start));
                state->queries++;
                state->expected_hits += result.hit ? 1U : 0U;
            }

            // record the retained geometry and submit
            state->gpu->draw(state->node);
            state->gpu->end_frame();
            state->frames_drawn++;

            // collect what the frame cost
            const GpuFrameTiming& timing = state->gpu->last_timing();
            if (timing.interval_ms > 0.0) {
                state->intervals.push_back(timing.interval_ms);
            }
            state->record.push_back(timing.record_ms);
            state->acquire.push_back(timing.acquire_ms);
        });

        // report the workload it actually ran
        std::printf("  frames offered %zu, drawn %zu, hover queries %zu\n", state->frames_offered, state->frames_drawn, state->queries);

        // report every recorded threshold
        const Stats record_stats = summarize(state->record);
        const Stats query_stats = summarize(state->query);
        const Stats interval_stats = summarize(state->intervals);
        report("recording the frame", record_stats, RECORD_BUDGET_MS, record_stats.p99 <= RECORD_BUDGET_MS, "ms");
        report("hover hit query on the ui thread", query_stats, W2_QUERY_BUDGET_MS, query_stats.p99 <= W2_QUERY_BUDGET_MS, "ms");
        report("present interval", interval_stats, INTERVAL_BUDGET_MS, interval_stats.p99 <= INTERVAL_BUDGET_MS, "ms");
        report("acquiring the drawable (t_acquire)", summarize(state->acquire), 0.0, true, "ms");

        // the hit ratio, which is a correctness threshold as much as a performance one
        const double hit_ratio = state->queries > 0 ? static_cast<double>(state->expected_hits) / static_cast<double>(state->queries) : 0.0;
        report_scalar("queries that hit the expected symbol", hit_ratio, W2_HIT_RATIO_BUDGET, hit_ratio >= W2_HIT_RATIO_BUDGET, "  ");
        gate(hit_ratio >= W2_HIT_RATIO_BUDGET);
    }

    // workload w3: two windows, with a hundred millisecond presentation hold injected into the first one
    void run_w3(albor::a2::PlatformWindow& first, albor::a2::PlatformWindow& second) {
        using namespace albor::a1;

        std::printf("\n== W3 multi-window isolation, a %zu ms presentation hold on window 1\n", W3_HOLD_MS);
        std::fflush(stdout);

        // take both windows main created, which is the isolation question of §9.2
        first.set_present_on_ui_thread(false);
        second.set_present_on_ui_thread(false);

        // bring up one renderer per window, because each window owns its own surface; both contexts are
        // deliberately never destroyed, for the same reason the windows are not
        auto* first_gpu = new GpuContext();
        auto* second_gpu = new GpuContext();
        if (!first_gpu->create(first) || !second_gpu->create(second)) {
            std::printf("  could not create both gpu contexts\n");
            gate(false);
            return;
        }

        // build one retained set and share it with both windows, because it is immutable (§10.5)
        ResourceFactory factory;
        LineBatchSpec spec;
        spec.segment_count = W1_INITIAL_SEGMENTS;
        spec.extent = WORKLOAD_EXTENT;
        spec.stroke_width = 1.0F;
        spec.seed = 0xBEEF0001ULL;
        const ResourceSetPtr resources = factory.create_line_batch(spec);
        if (resources == nullptr) {
            std::printf("  could not build the shared retained set\n");
            gate(false);
            return;
        }

        // each window owns its own frame state, because the view transforms and the injected hold are per window
        auto first_state = std::make_shared<FrameState>();
        auto second_state = std::make_shared<FrameState>();
        first_state->gpu = first_gpu;
        first_state->node = CustomRenderNode(resources);
        second_state->gpu = second_gpu;
        second_state->node = CustomRenderNode(resources);
        second_state->probe.tolerance = 4.0F;

        // the hold window samples are owned by the callback as well, so the vector cannot outlive the loop
        auto during_hold = std::make_shared<std::vector<double>>();
        auto queries_during_hold = std::make_shared<std::size_t>(0);

        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(RUN_TIMEOUT_MS);

        // window 2 is driven by the display link alone, with no loop of its own
        second.set_frame_callback([second_state, first_state, during_hold, queries_during_hold](const albor::a2::FrameRecord&) {
            // wrap, record and submit, which is the frame window 2 would present if it were not starved
            if (!second_state->gpu->begin_frame()) {
                return;
            }
            second_state->frames_drawn++;

            // move the view and do the ui thread work window 2 owes an input event
            second_state->node.set_view(moving_view(second_state->frames_drawn));
            second_state->probe.x = second_state->node.view().offset_x + 640.0F;
            second_state->probe.y = second_state->node.view().offset_y + 360.0F;
            static_cast<void>(second_state->node.hit_test(second_state->probe));
            second_state->queries++;
            second_state->gpu->draw(second_state->node);
            second_state->gpu->end_frame();

            // attribute the frame to the injected hold when one was active
            const GpuFrameTiming& timing = second_state->gpu->last_timing();
            if (timing.interval_ms > 0.0) {
                second_state->intervals.push_back(timing.interval_ms);
                if (first_state->holding) {
                    during_hold->push_back(timing.interval_ms);
                    (*queries_during_hold)++;
                }
            }
        });

        // window 1 owns the loop and injects the hold, against a cumulative frame target
        const std::size_t first_target = first.frames_presented() + WORKLOAD_FRAMES;
        first.close_after_frames(first_target);
        second.show();
        first.show();
        first.run([first_state, first_ptr = &first, deadline](const albor::a2::FrameRecord&) {
            // count what window 1 attempted, because its own drop budget is a ratio
            first_state->frames_offered++;

            // stop the run when the deadline passes, so a stalled platform cannot hang the prototype
            if (Clock::now() > deadline) {
                first_ptr->close_after_frames(1);
                return;
            }

            // inject the hold over a stretch of frames in the middle of the run
            const bool in_window = first_state->frames_offered > WORKLOAD_FRAMES / 4 && first_state->frames_offered <= WORKLOAD_FRAMES / 2;

            // open and close the hold window, so a window 2 frame can be attributed to it
            if (in_window && !first_state->holding) {
                first_state->hold_begin = Clock::now();
                first_state->hold_starts++;
            }
            if (!in_window && first_state->holding) {
                first_state->hold_end = Clock::now();
            }
            first_state->holding = in_window;

            // wrap, record and submit, with the hold applied to the presenting thread
            first_state->gpu->set_presentation_hold_ms(in_window ? static_cast<double>(W3_HOLD_MS) : 0.0);
            if (!first_state->gpu->begin_frame()) {
                return;
            }
            first_state->node.set_view(moving_view(first_state->frames_offered));
            first_state->gpu->draw(first_state->node);
            first_state->gpu->end_frame();
            first_state->frames_drawn++;
        });

        // make sure the hold window is closed even when the run ended inside it
        if (first_state->holding) {
            first_state->hold_end = Clock::now();
        }

        // detach both frame sources before this workload's locals go away: window 2 was driven by window 1's
        // loop, so its own source is still attached and would otherwise fire this callback after the returns
        first.detach_frame_source();
        second.detach_frame_source();

        // report what each window actually presented
        std::printf("  window 1 attempts %zu, presented %zu\n", first_state->frames_offered, first_state->frames_drawn);
        std::printf("  window 2 presented %zu, of which %zu inside the hold\n", second_state->frames_drawn, during_hold->size());

        // report the injected hold itself, so the reader can see the stall really happened
        const double hold_ms = to_ms(first_state->hold_end - first_state->hold_begin);
        std::printf("  %-44s %10zu windows, %.2f ms wide\n", "injected hold", first_state->hold_starts, hold_ms);

        // report the isolated window as a whole
        const Stats second_stats = summarize(second_state->intervals);
        report("window 2 present interval, whole run", second_stats, INTERVAL_BUDGET_MS, second_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // report the frames window 2 lost inside the hold window, against what the display offered
        const double expected_frames = hold_ms / INTERVAL_BUDGET_MS;
        const double presented_ratio = expected_frames > 0.0 ? static_cast<double>(during_hold->size()) / expected_frames : 0.0;
        report_scalar("window 2 frames presented during the hold", presented_ratio, W3_HOLD_RATIO_BUDGET, presented_ratio >= W3_HOLD_RATIO_BUDGET, "  ");

        // report the cadence window 2 actually kept while window 1 was stalled
        const Stats during_stats = summarize(*during_hold);
        report("window 2 present interval during the hold", during_stats, INTERVAL_BUDGET_MS, during_stats.p99 <= INTERVAL_BUDGET_MS, "ms");

        // report the ui thread work window 2 still managed, which is the input and layout half of the criterion
        report_scalar("window 2 hover queries during the hold", static_cast<double>(*queries_during_hold), W3_QUERY_FLOOR, static_cast<double>(*queries_during_hold) >= W3_QUERY_FLOOR, "  ");

        // report window 1's own drop ratio, because a stalled window 1 is not a failure by itself
        const double first_ratio = first_state->frames_offered > 0 ? static_cast<double>(first_state->frames_offered - first_state->frames_drawn) / static_cast<double>(first_state->frames_offered) : 1.0;
        report_scalar("window 1 frames dropped ratio", first_ratio, DROP_RATIO_BUDGET, first_ratio <= DROP_RATIO_BUDGET, "  ");
    }

} // namespace

int main() {
    // report the phase and the renderer it measured
    std::printf("Albor Prototype A1.3: skia workloads W1 to W3 (gates from prototypes/a1/plan.md)\n");
    std::fflush(stdout);

    // create every window the run needs before the first run loop starts, because appkit does not
    // tolerate being asked for a window once its run loop has stopped; the windows are deliberately
    // never destroyed, because this process reports its numbers and exits
    auto* primary = new albor::a2::PlatformWindow();
    auto* secondary = new albor::a2::PlatformWindow();
    if (!open_window(*primary, "Albor A1 workloads")) {
        std::printf("\nthreshold failures: %d\n", failures.load());
        return 1;
    }
    if (!open_window(*secondary, "Albor A1 W3 window 2")) {
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
    std::fflush(stdout);
    std::printf("\nthreshold failures: %d\n", failures.load());
    return failures.load() == 0 ? 0 : 1;
}
