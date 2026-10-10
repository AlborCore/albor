#include <memory>

#include <gtest/gtest.h>

#include "animation/animation_engine.h"
#include "animation/lerp.h"
#include "runtime/binding_engine.h"
#include "runtime/property.h"
#include "ui/frame_pipeline.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"

using namespace albor;

namespace
{
    // the node test double, which gives the animation something to invalidate (§6.1)
    class AnimationTestNode final : public UiNode
    {
    public:
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the node fills whatever it is offered, because the animation tests do not exercise layout (§8.2)
            out_size = constraints.constrain(Size{10.0f, 10.0f});
            m_measured_size = out_size;
        }
    };
} // namespace

TEST(LerpChecks, LinearAtTheEnds) {
    // arrange act
    const float at_start = lerp(2.0f, 8.0f, 0.0f);
    const float at_end = lerp(2.0f, 8.0f, 1.0f);
    // assert
    EXPECT_FLOAT_EQ(at_start, 2.0f);
    EXPECT_FLOAT_EQ(at_end, 8.0f);
}

TEST(LerpChecks, LinearAtTheMidpoint) {
    // arrange act
    const float middle = lerp(0.0f, 10.0f, 0.5f);
    // assert
    EXPECT_FLOAT_EQ(middle, 5.0f);
}

TEST(LerpChecks, ExtrapolatesBeyondTheUnitInterval) {
    // arrange act
    const float past = lerp(0.0f, 10.0f, 2.0f);
    // assert
    EXPECT_FLOAT_EQ(past, 20.0f);
}

TEST(LerpChecks, SmoothStepIsFlatAtTheEnds) {
    // arrange act
    const float at_start = ease_in_out(0.0f);
    const float at_end = ease_in_out(1.0f);
    const float middle = ease_in_out(0.5f);
    // assert
    EXPECT_FLOAT_EQ(at_start, 0.0f);
    EXPECT_FLOAT_EQ(at_end, 1.0f);
    EXPECT_FLOAT_EQ(middle, 0.5f);
}

TEST(LerpChecks, SmoothStepClampsOutsideTheUnitInterval) {
    // arrange act
    const float below = ease_in_out(-1.0f);
    const float above = ease_in_out(2.0f);
    // assert
    EXPECT_FLOAT_EQ(below, 0.0f);
    EXPECT_FLOAT_EQ(above, 1.0f);
}

TEST(LerpChecks, ColourIsInterpolatedComponentWise) {
    // arrange
    const Color black = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    const Color white = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f);
    // act
    const Color middle = lerp(black, white, 0.5f);
    // assert
    EXPECT_FLOAT_EQ(middle.red, 0.5f);
    EXPECT_FLOAT_EQ(middle.green, 0.5f);
    EXPECT_FLOAT_EQ(middle.blue, 0.5f);
    EXPECT_FLOAT_EQ(middle.alpha, 0.5f);
}

TEST(LerpChecks, ColourAtTheEndsReturnsTheInputs) {
    // arrange
    const Color lhs = Color::rgb(0.2f, 0.4f, 0.6f);
    const Color rhs = Color::rgb(0.8f, 0.9f, 1.0f);
    // act
    const Color at_start = lerp(lhs, rhs, 0.0f);
    const Color at_end = lerp(lhs, rhs, 1.0f);
    // assert
    EXPECT_FLOAT_EQ(at_start.red, lhs.red);
    EXPECT_FLOAT_EQ(at_end.blue, rhs.blue);
}

TEST(AnimationEngineChecks, StartsWithNothingRunning) {
    // arrange
    AnimationEngine engine;
    // act assert
    EXPECT_EQ(engine.active_count(), 0u);
}

TEST(AnimationEngineChecks, ScheduleReturnsAHandle) {
    // arrange
    AnimationEngine engine;
    // act
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(100));
    // assert
    EXPECT_NE(handle, 0u);
    EXPECT_TRUE(engine.running(handle));
    EXPECT_EQ(engine.active_count(), 1u);
}

TEST(AnimationEngineChecks, ScheduleRefusesATicklessAnimation) {
    // arrange
    AnimationEngine engine;
    // act
    const AnimationHandle handle = engine.schedule(nullptr, std::chrono::milliseconds(100));
    // assert
    EXPECT_EQ(handle, 0u);
    EXPECT_EQ(engine.active_count(), 0u);
}

TEST(AnimationEngineChecks, ScheduleRefusesAZeroDuration) {
    // arrange
    AnimationEngine engine;
    // act
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(0));
    // assert
    EXPECT_EQ(handle, 0u);
    EXPECT_EQ(engine.active_count(), 0u);
}

TEST(AnimationEngineChecks, ScheduleRefusesANegativeDuration) {
    // arrange
    AnimationEngine engine;
    // act
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(-50));
    // assert
    EXPECT_EQ(handle, 0u);
}

TEST(AnimationEngineChecks, AdvanceReportsNormalizedProgress) {
    // arrange
    AnimationEngine engine;
    std::vector<float> seen;
    (void)engine.schedule(
        [&seen](float t) {
            seen.push_back(t);
            return true;
        },
        std::chrono::milliseconds(200));
    // act
    engine.advance(0.1f);
    // assert
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_FLOAT_EQ(seen.front(), 0.5f);
}

TEST(AnimationEngineChecks, AdvanceCountsTheTicksItRan) {
    // arrange
    AnimationEngine engine;
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    // act
    const std::size_t ticked = engine.advance(0.1f);
    // assert
    EXPECT_EQ(ticked, 2u);
}

TEST(AnimationEngineChecks, TheTickSeesOneWhenAFrameOvershoots) {
    // arrange
    AnimationEngine engine;
    float seen = -1.0f;
    (void)engine.schedule(
        [&seen](float t) {
            seen = t;
            return true;
        },
        std::chrono::milliseconds(100));
    // act
    engine.advance(10.0f);
    // assert a frame longer than the whole animation lands the tick on one rather than past it (§7.3)
    EXPECT_FLOAT_EQ(seen, 1.0f);
}

TEST(AnimationEngineChecks, ProgressReportsTheElapsedFractionWhileRunning) {
    // arrange
    AnimationEngine engine;
    // the duration is long enough that the animation survives the frame, so the ratio is readable (§7.3)
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    // act
    engine.advance(0.5f);
    // assert
    EXPECT_FLOAT_EQ(engine.progress(handle), 0.5f);
}

TEST(AnimationEngineChecks, ProgressOfAnUnknownHandleIsZero) {
    // arrange
    AnimationEngine engine;
    // act
    const float progress = engine.progress(9999);
    // assert
    EXPECT_FLOAT_EQ(progress, 0.0f);
}

TEST(AnimationEngineChecks, RunningIsFalseForAnUnknownHandle) {
    // arrange
    AnimationEngine engine;
    // act
    const bool running = engine.running(4242);
    // assert
    EXPECT_FALSE(running);
}

TEST(AnimationEngineChecks, FinishesWhenTheDurationElapses) {
    // arrange
    AnimationEngine engine;
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(100));
    // act
    engine.advance(0.2f);
    // assert the entry is dropped, so the handle no longer names a running animation (§7.3)
    EXPECT_EQ(engine.active_count(), 0u);
    EXPECT_FALSE(engine.running(handle));
}

TEST(AnimationEngineChecks, EndsEarlyWhenTheTickRefuses) {
    // arrange
    AnimationEngine engine;
    const AnimationHandle handle = engine.schedule([](float) { return false; }, std::chrono::milliseconds(1000));
    // act
    engine.advance(0.01f);
    // assert
    EXPECT_EQ(engine.active_count(), 0u);
    EXPECT_FALSE(engine.running(handle));
}

TEST(AnimationEngineChecks, CancelStopsARunningAnimation) {
    // arrange
    AnimationEngine engine;
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    // act
    const bool cancelled = engine.cancel(handle);
    // assert
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(engine.running(handle));
}

TEST(AnimationEngineChecks, CancelIsFalseForAnUnknownHandle) {
    // arrange
    AnimationEngine engine;
    // act
    const bool cancelled = engine.cancel(7777);
    // assert
    EXPECT_FALSE(cancelled);
}

TEST(AnimationEngineChecks, CancelAllDropsEverything) {
    // arrange
    AnimationEngine engine;
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    // act
    engine.cancel_all();
    // assert
    EXPECT_EQ(engine.active_count(), 0u);
}

TEST(AnimationEngineChecks, CancelAllOnAnIdleEngineDoesNothing) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    // act
    engine.cancel_all();
    // assert
    EXPECT_EQ(requests, 0u);
}

TEST(AnimationEngineChecks, AdvanceOnAnIdleEngineDoesNothing) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    // act
    const std::size_t ticked = engine.advance(0.5f);
    // assert
    EXPECT_EQ(ticked, 0u);
    EXPECT_EQ(requests, 0u);
}

TEST(AnimationEngineChecks, SchedulingRequestsTheFirstFrame) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    // act
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(100));
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(AnimationEngineChecks, AStillRunningAnimationRequestsAnotherFrame) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    requests = 0;
    // act
    engine.advance(0.1f);
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(AnimationEngineChecks, CancelRequestsARedrawOfTheFinalValue) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    const AnimationHandle handle = engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    requests = 0;
    // act
    engine.cancel(handle);
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(AnimationEngineChecks, CancelAllRequestsARedrawOfTheFinalValues) {
    // arrange
    AnimationEngine engine;
    std::size_t requests = 0;
    engine.set_invalidation_handler([&requests] { requests++; });
    (void)engine.schedule([](float) { return true; }, std::chrono::milliseconds(1000));
    requests = 0;
    // act
    engine.cancel_all();
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(AnimationEngineChecks, AnimationsAdvanceIndependently) {
    // arrange
    AnimationEngine engine;
    std::vector<float> fast;
    std::vector<float> slow;
    (void)engine.schedule(
        [&fast](float t) {
            fast.push_back(t);
            return true;
        },
        std::chrono::milliseconds(100));
    (void)engine.schedule(
        [&slow](float t) {
            slow.push_back(t);
            return true;
        },
        std::chrono::milliseconds(400));
    // act
    engine.advance(0.1f);
    // assert
    ASSERT_EQ(fast.size(), 1u);
    ASSERT_EQ(slow.size(), 1u);
    EXPECT_FLOAT_EQ(fast.front(), 1.0f);
    EXPECT_FLOAT_EQ(slow.front(), 0.25f);
}

TEST(AnimationEngineChecks, WritesThroughThePropertyAnimationChannel) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    (void)base;
    const PropertyId property_id = engine.next_property_id();
    Property<float> value{0.0f, property_id, &engine};
    AnimationEngine animations;
    (void)animations.schedule(
        [&value](float t) {
            // the animation writes the presence channel, which is the only channel an animation may use (AD-04d)
            value.animate(t);
            return true;
        },
        std::chrono::milliseconds(1000));
    // act
    animations.advance(0.5f);
    // assert
    EXPECT_FLOAT_EQ(value.get(), 0.5f);
    EXPECT_TRUE(value.presence().active);
}

TEST(AnimationEngineChecks, AnAnimationYieldsToAnImperativeOverride) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    (void)base;
    const PropertyId property_id = engine.next_property_id();
    Property<float> value{0.0f, property_id, &engine};
    AnimationEngine animations;
    (void)animations.schedule(
        [&value](float t) {
            // the animation writes the presence channel, which yields to an override (AD-24, AD-04d)
            value.animate(t);
            return true;
        },
        std::chrono::milliseconds(1000));
    // act
    value.set(9.0f);
    animations.advance(0.5f);
    // assert
    EXPECT_FLOAT_EQ(value.get(), 9.0f);
}

TEST(FramePipelineAnimationChecks, StartsWithoutAnEngine) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    // act assert
    EXPECT_EQ(pipeline.animation_engine(), nullptr);
}

TEST(FramePipelineAnimationChecks, HandsBackTheAttachedEngine) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    AnimationEngine animations;
    // act
    pipeline.set_animation_engine(&animations);
    // assert
    EXPECT_EQ(pipeline.animation_engine(), &animations);
}

TEST(FramePipelineAnimationChecks, AdvancesTheAttachedAnimations) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    AnimationEngine animations;
    pipeline.set_animation_engine(&animations);
    std::vector<float> seen;
    (void)animations.schedule(
        [&seen](float t) {
            seen.push_back(t);
            return true;
        },
        std::chrono::milliseconds(1000));
    // act
    pipeline.advance_animations(0.25f);
    // assert
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_FLOAT_EQ(seen.front(), 0.25f);
}

TEST(FramePipelineAnimationChecks, AdvancesWithoutAnEngine) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    // act
    pipeline.advance_animations(0.25f);
    // assert the pipeline without an engine simply does nothing, which is the phase 2 shape (§7.3)
    EXPECT_EQ(pipeline.animation_engine(), nullptr);
}

TEST(FramePipelineAnimationChecks, SettlesTheGraphAfterAdvancing) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    (void)base;
    const PropertyId property_id = engine.next_property_id();
    Property<float> source{0.0f, property_id, &engine};
    Property<float> derived{0.0f, property_id, &engine};
    derived.bind([&source] { return source.get() * 2.0f; }, {source.id()});
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    AnimationEngine animations;
    pipeline.set_animation_engine(&animations);
    (void)animations.schedule(
        [&source](float t) {
            // the animation drives the source property, so a binding on it must see the animated value (§7.3)
            source.animate(t);
            return true;
        },
        std::chrono::milliseconds(1000));
    // act
    pipeline.advance_animations(0.5f);
    // assert
    EXPECT_FLOAT_EQ(source.get(), 0.5f);
    EXPECT_FLOAT_EQ(derived.get(), 1.0f);
}

TEST(FramePipelineAnimationChecks, ABuiltFrameCarriesTheAnimatedValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    (void)base;
    const PropertyId property_id = engine.next_property_id();
    Property<float> opacity{0.0f, property_id, &engine};
    FramePipeline pipeline(engine, std::make_unique<AnimationTestNode>());
    AnimationEngine animations;
    pipeline.set_animation_engine(&animations);
    (void)animations.schedule(
        [&opacity](float t) {
            // the animation drives the property the pipeline reads while it settles (§6.1, §7.3)
            opacity.animate(t);
            return true;
        },
        std::chrono::milliseconds(1000));
    // act
    pipeline.advance_animations(0.75f);
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_FLOAT_EQ(opacity.get(), 0.75f);
}
