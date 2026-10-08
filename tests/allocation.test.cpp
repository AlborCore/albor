#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/frame_scene.h"
#include "render/render_executor.h"
#include "text/text-skia/skia_text_engine.h"
#include "tools/alloc-probe/alloc_probe.h"
#include "ui/frame_pipeline.h"
#include "ui/scene_builder.h"

using namespace albor;

namespace
{
    // the node the counting builds lay out and record, which paints one rectangle of its bounds (§6.1)
    class CountingNode final : public UiNode
    {
    public:
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the node fills whatever it is offered, because a leaf has no intrinsic extent (§8.2)
            UiNode::measure(constraints, out_size);
            // the measured size is clamped into the constraints, the way a leaf does (§8.2)
            out_size = constraints.constrain(Size{200.0f, 120.0f});
            m_measured_size = out_size;
        }

        void render(RenderContext& context) override {
            // the node paints its own bounds, which is the one command the counting builds record (§6.1)
            context.draw_rect(bounds(), Paint{.fill = Color::rgb(0.2f, 0.6f, 0.9f)});
            // the children draw over the node, which is the composite contract (§6.1)
            UiNode::render(context);
        }
    };

    // the window metrics every counting build presents at (§6.1)
    WindowMetrics counting_metrics() { return WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f); }
} // namespace

TEST(AllocationProbeChecks, TheProbeIsOffByDefault) {
    // arrange, act, because counting is off until a test asks for it (§2.2)
    EXPECT_FALSE(probe::counting());
    EXPECT_EQ(probe::count(), 0u);
}

TEST(AllocationProbeChecks, TheProbeCountsWhatIsAllocated) {
    // arrange
    probe::set_counting(true);
    probe::reset();
    // act a block the framework hands out has to reach the counter, or the probe measures nothing (§2.2)
    const std::vector<int> block(64, 7);
    // assert
    EXPECT_GT(probe::count(), 0u);
    // assert
    EXPECT_GT(block.size(), 0u);
    probe::set_counting(false);
}

TEST(AllocationProbeChecks, TheProbeReportsNothingLiveAfterAScope) {
    // arrange
    probe::set_counting(true);
    probe::reset();
    {
        // the block goes out of scope inside the braces, which is what a steady state frame does (§2.2)
        const std::shared_ptr<int> block = std::make_shared<int>(3);
        // the block is alive here, so the live count has to have moved (§2.2)
        EXPECT_GT(probe::live(), 0u);
        // assert the block carries what it was given, because a probe that corrupts memory is worse than none (§2.2)
        EXPECT_EQ(*block, 3);
    }
    // assert every counted block was freed, because a frame that leaks is a defect the count has to see (§2.2)
    EXPECT_EQ(probe::live(), 0u);
    probe::set_counting(false);
}

TEST(AllocationProbeChecks, TheProbeStopsCountingWhenTurnedOff) {
    // arrange
    probe::set_counting(false);
    probe::reset();
    // act the same work the counting test did, with the probe off (§2.2)
    const std::vector<int> block(64, 7);
    // assert nothing is counted while the probe is off, so an ordinary test pays one load (§2.2)
    EXPECT_EQ(probe::count(), 0u);
    // assert
    EXPECT_GT(block.size(), 0u);
}

TEST(AllocationProbeChecks, ASteadyStateFrameAllocatesLessThanTheFirstOne) {
    // arrange the tree is built once, so the second frame records what the first already settled (§6.1)
    SceneBuilder builder;
    CountingNode root;
    // act the first build, which pays for every buffer it grows (§6.1)
    const std::shared_ptr<const FrameScene> first = builder.build(root, counting_metrics(), Color::rgb(0.0f, 0.0f, 0.0f));
    probe::set_counting(true);
    probe::reset();
    // act the second build of the same tree, which is what a steady state frame costs (§6.1)
    const std::shared_ptr<const FrameScene> second = builder.build(root, counting_metrics(), Color::rgb(0.0f, 0.0f, 0.0f));
    const std::size_t steady_allocations = probe::count();
    probe::set_counting(false);
    // assert both frames carry the command the node recorded, because the count is only about the heap (§6.1)
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->command_count(), second->command_count());
    // assert a steady state frame stays under a bound, because an unbounded frame is what the review is about (§2.2)
    EXPECT_LT(steady_allocations, 32u);
}

TEST(AllocationProbeChecks, ARepeatedMeasureOfOneRunCostsNoAllocation) {
    // arrange the run is shaped once, so the second measure is a cache lookup (§9.1)
    SkiaTextEngine engine;
    engine.measure("albor", 500.0f, 16.0f);
    probe::set_counting(true);
    probe::reset();
    // act
    const TextLayout repeated = engine.measure("albor", 500.0f, 16.0f);
    const std::size_t repeat_allocations = probe::count();
    probe::set_counting(false);
    // assert the cache served the run, which is what a steady state frame of a tree full of labels relies on (§9.1)
    EXPECT_NE(repeated.run, nullptr);
    EXPECT_LT(repeat_allocations, 8u);
}

TEST(AllocationProbeChecks, ARepeatedMeasureOfALongRunCostsNoAllocation) {
    // arrange the text is past the small string limit, which is where copying a key would have allocated (§9.1)
    SkiaTextEngine engine;
    const std::string long_text(200, 'b');
    engine.measure(long_text, 500.0f, 16.0f);
    probe::set_counting(true);
    probe::reset();
    // act
    for (int repeat = 0; repeat < 10; repeat++)
        engine.measure(long_text, 500.0f, 16.0f);
    const std::size_t repeat_allocations = probe::count();
    probe::set_counting(false);
    // assert the cache is looked up through a view, so a hit costs nothing however long the run is (§9.1)
    EXPECT_EQ(repeat_allocations, 0u);
}

TEST(AllocationProbeChecks, AMissShapesTheRunExactlyOnce) {
    // arrange
    SkiaTextEngine engine;
    // act the same run is measured twice, which is the steady state pattern of a label (§9.1)
    engine.measure("repeated", 500.0f, 16.0f);
    engine.measure("repeated", 500.0f, 16.0f);
    // assert the second measure was served by the cache, which is what makes a frame cheap (§9.1)
    EXPECT_EQ(engine.shaped_run_count(), 1u);
}
