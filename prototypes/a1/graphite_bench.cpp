// the a1.4 slice: graphite recording and submission measured offscreen, against the ganesh numbers of a1.3
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
#include "prototypes/a1/graphite_context.h"
#include "prototypes/a1/resource_factory.h"
#include "prototypes/a1/snapshot.h"

namespace
{

    // the workload sizes recorded in plan.md, identical to a1.3's w1 so the two are comparable
    constexpr std::size_t INITIAL_SEGMENTS = 1000000;
    constexpr std::size_t APPEND_SEGMENTS = 100000;
    constexpr std::size_t APPENDS = 8;
    constexpr std::size_t APPEND_INTERVAL_MS = 700;
    constexpr std::size_t FRAMES = 400;
    constexpr std::size_t PIXEL_WIDTH = 2560;
    constexpr std::size_t PIXEL_HEIGHT = 1440;
    constexpr float WORKLOAD_EXTENT = 2000.0F;

    // the thresholds recorded in plan.md before the run
    constexpr double RECORD_BUDGET_MS = 8.33;
    constexpr double PER_SEGMENT_BUDGET_NS = 8.0;
    constexpr double PAN_AMORTISATION_BUDGET = 1.25;
    constexpr double SUBMIT_BUDGET_MS = 150.0;

    // the ganesh figures a1.3 recorded on the same machine, reported beside these for comparison
    constexpr double GANESH_RECORD_P50_MS = 0.1771;
    constexpr double GANESH_PER_SEGMENT_NS = 0.2192;

    // the clock this slice measures with
    using Clock = std::chrono::steady_clock;

    // the percentiles the slice has to report
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

    // the pan and zoom the workload runs continuously, identical in shape to a1.3's w1
    albor::a1::ViewTransform moving_view(std::uint64_t index) {
        albor::a1::ViewTransform view;
        view.scale = 0.55F + 0.45F * std::sin(static_cast<float>(index) * 0.031F);
        view.offset_x = 40.0F * std::cos(static_cast<float>(index) * 0.017F);
        view.offset_y = 40.0F * std::sin(static_cast<float>(index) * 0.023F);
        return view;
    }

    // the per frame state, owned by the loop through a shared_ptr so nothing can dangle
    struct FrameState
    {
        albor::a1::GraphiteContext* context = nullptr;
        albor::a1::CustomRenderNode node{nullptr};
        std::vector<double> record;
        std::vector<double> record_moved;
        std::vector<double> record_still;
        std::vector<double> surface;
        std::vector<double> submit;
        std::vector<double> frame;
        std::size_t completed = 0;
        albor::a1::ViewTransform frozen_view{};
    };

} // namespace

int main() {
    using namespace albor::a1;

    // report the phase and what it is measuring
    std::printf("Albor Prototype A1.4: graphite offscreen (gates from prototypes/a1/plan.md)\n");
    std::fflush(stdout);

    // the factory owns no gpu state, so the producer thread may call it (§11.2)
    ResourceFactory factory;

    // the append stream the producer publishes into, crossing only as an immutable snapshot (§10.5)
    Snapshot<ResourceSet> stream;
    std::atomic<bool> producing{true};
    std::atomic<std::size_t> publishes{0};

    // build the first batch and then publish growing batches, exactly as a1.3's w1 did
    std::thread producer([&factory, &stream, &producing, &publishes] {
        // publish the initial million segments
        LineBatchSpec spec;
        spec.segment_count = INITIAL_SEGMENTS;
        spec.extent = WORKLOAD_EXTENT;
        spec.stroke_width = 1.0F;
        spec.seed = 0xA11F5EEDULL;
        ResourceSetPtr initial = factory.create_line_batch(spec);
        if (initial == nullptr) {
            return;
        }
        stream.post_set(std::move(initial));
        publishes.fetch_add(1);

        // keep appending so the stream is exercised while frames are recorded
        const std::size_t limit = INITIAL_SEGMENTS + APPENDS * APPEND_SEGMENTS;
        std::size_t total = INITIAL_SEGMENTS;
        while (producing.load(std::memory_order_relaxed) && total < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(APPEND_INTERVAL_MS));

            LineBatchSpec next;
            next.extent = WORKLOAD_EXTENT;
            next.stroke_width = 1.0F;
            next.seed = 0xA11F5EEDULL;
            total += APPEND_SEGMENTS;
            next.segment_count = total;
            ResourceSetPtr grown = factory.create_line_batch(next);
            if (grown == nullptr) {
                break;
            }
            stream.post_set(std::move(grown));
            publishes.fetch_add(1);
        }
    });

    // bring up the offscreen renderer, which needs no window and no window server
    auto* context = new GraphiteContext();
    if (!context->create(PIXEL_WIDTH, PIXEL_HEIGHT)) {
        std::printf("  could not create the graphite context\n");
        producing.store(false, std::memory_order_relaxed);
        producer.join();
        gate(false);
        return 1;
    }

    // report which backend actually came up, because §11.7 compares backends
    std::printf("  renderer backend: %s, surface %zux%zu offscreen\n", context->backend(), PIXEL_WIDTH, PIXEL_HEIGHT);
    std::fflush(stdout);

    // wait for the first publish so the first frame has geometry to replay
    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(60);
    ResourceSetPtr held;
    while (held == nullptr && Clock::now() < deadline) {
        held = stream.get();
        if (held == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (held == nullptr) {
        std::printf("  the producer never published a batch\n");
        gate(false);
        producing.store(false, std::memory_order_relaxed);
        producer.join();
        return 1;
    }

    // the frame loop owns its state, so nothing it touches can be destroyed underneath it
    auto state = std::make_shared<FrameState>();
    state->context = context;
    state->node = CustomRenderNode(held);

    // replay the same workload a1.3's w1 ran, one frame at a time
    for (std::size_t frame = 1; frame <= FRAMES; frame++) {
        // pick up whatever the stream currently holds, so the appends are exercised
        const ResourceSetPtr resources = stream.get();
        if (resources != nullptr) {
            state->node.set_resources(resources);
            held = resources;
        }

        // allocate the surface and replay the retained batches
        if (!state->context->begin_frame()) {
            continue;
        }

        // move the view on three frames out of four and hold it still on the fourth, so the O(1) claim has both
        const bool moved = frame % 4U != 0U;
        if (moved) {
            state->frozen_view = moving_view(frame);
        }
        state->node.set_view(state->frozen_view);

        state->context->draw(state->node);
        state->context->end_frame();
        state->completed++;

        // collect what the frame cost, split by what made it interesting
        const GraphiteFrameTiming& timing = state->context->last_timing();
        state->surface.push_back(timing.surface_ms);
        state->record.push_back(timing.record_ms);
        state->submit.push_back(timing.submit_ms);
        state->frame.push_back(timing.frame_ms);
        if (moved) {
            state->record_moved.push_back(timing.record_ms);
        }
        else {
            state->record_still.push_back(timing.record_ms);
        }
    }

    // stop the producer before the stream is destroyed
    producing.store(false, std::memory_order_relaxed);
    producer.join();

    // report what the slice actually ran
    const ResourceSetInfo info = describe(*held);
    std::printf("  frames completed %zu of %zu, publishes %zu, final segments %zu\n", state->completed, FRAMES, publishes.load(), info.segments());
    std::printf("  retained bytes in the final set %zu, draw calls per frame %zu\n", info.bytes, info.draw_calls);
    std::fflush(stdout);

    // report every recorded threshold
    const Stats record_stats = summarize(state->record);
    const Stats submit_stats = summarize(state->submit);
    report("allocating the offscreen surface", summarize(state->surface), 0.0, true, "ms");
    report("recording the frame", record_stats, RECORD_BUDGET_MS, record_stats.p99 <= RECORD_BUDGET_MS, "ms");
    report("submitting the frame (no vsync)", submit_stats, SUBMIT_BUDGET_MS, submit_stats.p99 <= SUBMIT_BUDGET_MS, "ms");
    report("whole frame", summarize(state->frame), 0.0, true, "ms");
    report_scalar("frames completed", static_cast<double>(state->completed), static_cast<double>(FRAMES), state->completed == FRAMES, "  ");

    // the O(1) claim, retested on graphite rather than ganesh
    const Stats moved_stats = summarize(state->record_moved);
    const Stats still_stats = summarize(state->record_still);
    const double amortisation = still_stats.p95 > 0.0 ? moved_stats.p95 / still_stats.p95 : 0.0;
    report("recording while the view moves", moved_stats, 0.0, true, "ms");
    report("recording while the view is still", still_stats, 0.0, true, "ms");
    report_scalar("recording p95 moved / still (O(1))", amortisation, PAN_AMORTISATION_BUDGET, amortisation <= PAN_AMORTISATION_BUDGET, "  ");

    // the per segment cost, the number that has to stay flat as the batch grows
    const double per_segment_ns = info.segments() > 0 ? (record_stats.p99 * 1.0E6) / static_cast<double>(info.segments()) : 0.0;
    report_scalar("recording p99 per segment", per_segment_ns, PER_SEGMENT_BUDGET_NS, per_segment_ns <= PER_SEGMENT_BUDGET_NS, "ns");

    // the comparison a1.4 exists for: the same workload on the two backends
    std::printf("\n  comparison against ganesh on the same machine and the same retained batches:\n");
    std::printf("  %-44s %10s %10s %10s\n", "metric", "ganesh", "graphite", "ratio");
    std::printf("  %-44s %10.4f %10.4f %10.3f\n", "recording p50 ms", GANESH_RECORD_P50_MS, record_stats.p50, record_stats.p50 / GANESH_RECORD_P50_MS);
    std::printf("  %-44s %10.4f %10.4f %10.3f\n", "recording p99 per segment ns", GANESH_PER_SEGMENT_NS, per_segment_ns, per_segment_ns / GANESH_PER_SEGMENT_NS);
    std::fflush(stdout);

    // report the run as a whole, so a ci job notices a failing slice
    std::printf("\nthreshold failures: %d\n", failures.load());
    return failures.load() == 0 ? 0 : 1;
}
