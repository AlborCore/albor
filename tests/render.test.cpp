#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/frame_scene.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"

using namespace albor;

namespace
{
    // the context test double, which records the state contract of a draw surface (§6.2)
    class TestRenderContext final : public RenderContext
    {
    public:
        // how many states were pushed
        std::size_t save_count = 0;

        // how many states were popped
        std::size_t restore_count = 0;

        // every transform the surface was asked to adopt, in order
        std::vector<Transform> transforms;

        // every axis aligned clip the surface was asked to adopt, in order
        std::vector<Rect> clips;

        // every rounded clip the surface was asked to adopt, in order
        std::vector<float> rounded_radii;

        // the rectangles the surface was asked to draw, in order
        std::vector<Rect> drawn_rects;

        // the text runs the surface was asked to draw, in order
        std::vector<std::string> drawn_texts;

        void save() override { save_count++; }

        void restore() override { restore_count++; }

        void set_transform(const Transform& transform) override {
            transforms.push_back(transform);
            current = transform;
        }

        [[nodiscard]] const Transform& transform() const override { return current; }

        void clip_rect(const Rect& rect) override { clips.push_back(rect); }

        void clip_rounded_rect(const Rect& rect, float radius) override {
            clips.push_back(rect);
            rounded_radii.push_back(radius);
        }

        [[nodiscard]] const Rect& clip() const override { return current_clip; }

        void draw_rect(const Rect& rect, const Paint&) override { drawn_rects.push_back(rect); }

        void draw_rounded_rect(const Rect& rect, float, const Paint&) override { drawn_rects.push_back(rect); }

        void draw_text(const TextLayout& layout, Point, const Paint&) override { drawn_texts.push_back(layout.text); }

    private:
        // the transform the surface reads back
        Transform current;

        // the clip the surface reads back
        Rect current_clip;
    };

    // the surface test double, which hands out the context above and counts its frames (§6.2)
    class TestRenderSurface final : public RenderSurface
    {
    public:
        // the context the surface hands out, which the test asserts against
        TestRenderContext context;

        // how many frames were begun
        std::size_t begun = 0;

        // how many frames were ended
        std::size_t ended = 0;

        // whether the surface accepts a frame at all
        bool is_valid = true;

        // the metrics the surface presents at, which a resize changes
        WindowMetrics surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);

        [[nodiscard]] bool valid() const override { return is_valid; }

        [[nodiscard]] const WindowMetrics& metrics() const override { return surface_metrics; }

        void resize(const WindowMetrics& metrics) override { surface_metrics = metrics; }

        RenderContext& begin_frame() override {
            begun++;
            return context;
        }

        void end_frame() override { ended++; }
    };

    // the backend test double, which records the scenes stage two executes (§6.2)
    class TestRendererBackend final : public RendererBackend
    {
    public:
        // the rectangles of every scene the backend was asked to execute, in order
        std::vector<std::size_t> executed_rect_counts;

        // whether the backend came up
        bool is_ready = true;

        [[nodiscard]] bool ready() const override { return is_ready; }

        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle, const WindowMetrics&) override { return std::make_unique<TestRenderSurface>(); }

        void resize_surface(RenderSurface&, const WindowMetrics&) override {}

        void execute_frame(RenderSurface& surface, const FrameScene& scene) override {
            // the double draws the scene the way a real backend would, so the executor is exercised end to end
            RenderContext& context = surface.begin_frame();
            context.save();
            context.set_transform(Transform::translation(scene.metrics().logical_size.width / 2.0f, scene.metrics().logical_size.height / 2.0f));
            for (const Rect& rect : scene.rects())
                context.draw_rect(rect, Paint{});
            context.restore();
            surface.end_frame();
            executed_rect_counts.push_back(scene.rects().size());
        }
    };
} // namespace

TEST(TextLayout, AnEmptyStringIsEmpty) {
    // arrange, act
    const TextLayout layout;
    // assert
    EXPECT_TRUE(layout.empty());
}

TEST(TextLayout, FromTextIsNotEmpty) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("albor", 16.0f);
    // assert
    EXPECT_FALSE(layout.empty());
}

TEST(TextLayout, FromTextKeepsTheText) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("albor", 16.0f);
    // assert
    EXPECT_EQ(layout.text, "albor");
}

TEST(TextLayout, FromTextMeasuresAWidthThatGrowsWithTheText) {
    // arrange
    const TextLayout short_run = TextLayout::from_text("ab", 16.0f);
    // act
    const TextLayout long_run = TextLayout::from_text("abcdefgh", 16.0f);
    // assert
    EXPECT_GT(long_run.size.width, short_run.size.width);
}

TEST(TextLayout, FromTextMeasuresAWidthThatGrowsWithTheFontSize) {
    // arrange
    const TextLayout small = TextLayout::from_text("albor", 8.0f);
    // act
    const TextLayout large = TextLayout::from_text("albor", 32.0f);
    // assert
    EXPECT_GT(large.size.width, small.size.width);
}

TEST(TextLayout, FromTextMeasuresAHeightEqualToTheFontSize) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("albor", 20.0f);
    // assert
    EXPECT_FLOAT_EQ(layout.size.height, 20.0f);
}

TEST(TextLayout, FromTextPlacesTheAscentAboveTheDescent) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("albor", 10.0f);
    // assert
    EXPECT_GT(layout.ascent, layout.descent);
}

TEST(TextLayout, TheAscentAndDescentAddUpToTheFontSize) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("albor", 25.0f);
    // assert
    EXPECT_FLOAT_EQ(layout.ascent + layout.descent, 25.0f);
}

TEST(TextLayout, FromAnEmptyStringMeasuresNoWidth) {
    // arrange, act
    const TextLayout layout = TextLayout::from_text("", 16.0f);
    // assert
    EXPECT_FLOAT_EQ(layout.size.width, 0.0f);
    EXPECT_TRUE(layout.empty());
}

TEST(TextLayout, EqualityComparesEveryField) {
    // arrange
    const TextLayout first = TextLayout::from_text("albor", 16.0f);
    const TextLayout same = TextLayout::from_text("albor", 16.0f);
    const TextLayout other = TextLayout::from_text("alborx", 16.0f);
    // act, assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}

TEST(FrameScene, ConstructionKeepsTheMetrics) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // act
    const FrameScene scene(metrics);
    // assert
    EXPECT_EQ(scene.metrics(), metrics);
}

TEST(FrameScene, AnEmptySceneHasNoPrimitives) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // act
    const FrameScene scene(metrics);
    // assert
    EXPECT_TRUE(scene.rects().empty());
    EXPECT_TRUE(scene.texts().empty());
}

TEST(FrameScene, FromPrimitivesCarriesTheMetrics) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    // act
    const FrameScene scene = FrameScene::from_primitives(metrics, Color::rgb(0.0f, 0.0f, 0.0f), {});
    // assert
    EXPECT_EQ(scene.metrics(), metrics);
}

TEST(FrameScene, FromPrimitivesCarriesTheBackground) {
    // arrange, act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.25f, 0.5f, 0.75f), {});
    // assert
    EXPECT_EQ(scene.background(), Color::rgb(0.25f, 0.5f, 0.75f));
}

TEST(FrameScene, FromPrimitivesKeepsEveryRectangle) {
    // arrange
    std::vector<Rect> rects;
    rects.push_back(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    rects.push_back(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    // act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // assert
    ASSERT_EQ(scene.rects().size(), 2u);
    EXPECT_EQ(scene.rects()[0], (Rect{0.0f, 0.0f, 10.0f, 10.0f}));
    EXPECT_EQ(scene.rects()[1], (Rect{10.0f, 10.0f, 20.0f, 20.0f}));
}

TEST(FrameScene, FromPrimitivesKeepsTheRectangleOrder) {
    // arrange
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    rects.push_back(Rect{1.0f, 1.0f, 2.0f, 2.0f});
    rects.push_back(Rect{2.0f, 2.0f, 3.0f, 3.0f});
    // act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // assert
    ASSERT_EQ(scene.rects().size(), 3u);
    EXPECT_EQ(scene.rects()[0], (Rect{0.0f, 0.0f, 1.0f, 1.0f}));
    EXPECT_EQ(scene.rects()[1], (Rect{1.0f, 1.0f, 2.0f, 2.0f}));
    EXPECT_EQ(scene.rects()[2], (Rect{2.0f, 2.0f, 3.0f, 3.0f}));
}

TEST(FrameScene, ASceneCarriesNoTextRuns) {
    // arrange, act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), {});
    // assert
    EXPECT_TRUE(scene.texts().empty());
}

TEST(FrameScene, AnUnconfiguredSceneKeepsTheDefaultBackground) {
    // arrange, act
    const FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // assert
    EXPECT_EQ(scene.background(), Color{});
    EXPECT_FLOAT_EQ(scene.background().alpha, 1.0f);
}

TEST(RenderContext, SaveAndRestoreAreCounted) {
    // arrange
    TestRenderContext context;
    // act
    context.save();
    context.restore();
    // assert
    EXPECT_EQ(context.save_count, 1u);
    EXPECT_EQ(context.restore_count, 1u);
}

TEST(RenderContext, TheTransformReadsBackWhatWasSet) {
    // arrange
    TestRenderContext context;
    const Transform transform = Transform::translation(3.0f, 4.0f);
    // act
    context.set_transform(transform);
    // assert
    EXPECT_EQ(context.transform(), transform);
}

TEST(RenderContext, ADefaultContextHasTheIdentityTransform) {
    // arrange, act
    const TestRenderContext context;
    // assert
    EXPECT_EQ(context.transform(), Transform{});
}

TEST(RenderContext, EveryTransformIsRecorded) {
    // arrange
    TestRenderContext context;
    // act
    context.set_transform(Transform::scaling(2.0f, 2.0f));
    context.set_transform(Transform::translation(1.0f, 1.0f));
    // assert
    ASSERT_EQ(context.transforms.size(), 2u);
    EXPECT_EQ(context.transforms[0], Transform::scaling(2.0f, 2.0f));
    EXPECT_EQ(context.transforms[1], Transform::translation(1.0f, 1.0f));
}

TEST(RenderContext, ClipRectIsRecorded) {
    // arrange
    TestRenderContext context;
    const Rect clip{0.0f, 0.0f, 100.0f, 100.0f};
    // act
    context.clip_rect(clip);
    // assert
    ASSERT_EQ(context.clips.size(), 1u);
    EXPECT_EQ(context.clips[0], clip);
}

TEST(RenderContext, ClipRoundedRectIsRecordedWithItsRadius) {
    // arrange
    TestRenderContext context;
    const Rect clip{0.0f, 0.0f, 100.0f, 100.0f};
    // act
    context.clip_rounded_rect(clip, 8.0f);
    // assert
    ASSERT_EQ(context.clips.size(), 1u);
    EXPECT_EQ(context.clips[0], clip);
    ASSERT_EQ(context.rounded_radii.size(), 1u);
    EXPECT_FLOAT_EQ(context.rounded_radii[0], 8.0f);
}

TEST(RenderContext, DrawRectIsRecorded) {
    // arrange
    TestRenderContext context;
    const Rect rect{1.0f, 2.0f, 3.0f, 4.0f};
    // act
    context.draw_rect(rect, Paint{});
    // assert
    ASSERT_EQ(context.drawn_rects.size(), 1u);
    EXPECT_EQ(context.drawn_rects[0], rect);
}

TEST(RenderContext, DrawRoundedRectIsRecorded) {
    // arrange
    TestRenderContext context;
    const Rect rect{1.0f, 2.0f, 3.0f, 4.0f};
    // act
    context.draw_rounded_rect(rect, 4.0f, Paint{});
    // assert
    ASSERT_EQ(context.drawn_rects.size(), 1u);
    EXPECT_EQ(context.drawn_rects[0], rect);
}

TEST(RenderContext, DrawTextIsRecorded) {
    // arrange
    TestRenderContext context;
    const TextLayout layout = TextLayout::from_text("albor", 16.0f);
    // act
    context.draw_text(layout, Point{0.0f, 0.0f}, Paint{});
    // assert
    ASSERT_EQ(context.drawn_texts.size(), 1u);
    EXPECT_EQ(context.drawn_texts[0], "albor");
}

TEST(RenderSurface, ABeginFrameHandsOutTheContext) {
    // arrange
    TestRenderSurface surface;
    // act
    RenderContext& context = surface.begin_frame();
    // assert
    EXPECT_EQ(surface.begun, 1u);
    EXPECT_EQ(dynamic_cast<TestRenderContext*>(&context), &surface.context);
}

TEST(RenderSurface, AnEndFrameIsCounted) {
    // arrange
    TestRenderSurface surface;
    surface.begin_frame();
    // act
    surface.end_frame();
    // assert
    EXPECT_EQ(surface.ended, 1u);
}

TEST(RenderSurface, AResizeUpdatesTheMetrics) {
    // arrange
    TestRenderSurface surface;
    // act
    surface.resize(WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 1.0f));
    // assert
    EXPECT_EQ(surface.metrics(), WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 1.0f));
}

TEST(RenderSurface, AnInvalidSurfaceIsNotValid) {
    // arrange
    TestRenderSurface surface;
    surface.is_valid = false;
    // act, assert
    EXPECT_FALSE(surface.valid());
}

TEST(RenderExecutor, AnExecutorWithoutABeBackendAcceptsNothing) {
    // arrange
    RenderExecutor executor(nullptr);
    TestRenderSurface surface;
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // act
    const bool submitted = executor.submit(surface, scene);
    // assert
    EXPECT_FALSE(submitted);
}

TEST(RenderExecutor, AnExecutorWithoutABeBackendExecutesNothing) {
    // arrange
    RenderExecutor executor(nullptr);
    // act, assert
    EXPECT_EQ(executor.executed_count(), 0u);
}

TEST(RenderExecutor, SubmittingASceneReachesTheBackend) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    const auto scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects)));
    // act
    const bool submitted = executor.submit(surface, scene);
    executor.flush();
    // assert
    EXPECT_TRUE(submitted);
    EXPECT_EQ(executor.executed_count(), 1u);
    ASSERT_EQ(backend.executed_rect_counts.size(), 1u);
    EXPECT_EQ(backend.executed_rect_counts[0], 1u);
}

TEST(RenderExecutor, TheBackendDrawsThroughTheSurfaceContext) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    const auto scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects)));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert
    EXPECT_EQ(surface.begun, 1u);
    EXPECT_EQ(surface.ended, 1u);
    EXPECT_EQ(surface.context.save_count, 1u);
    EXPECT_EQ(surface.context.restore_count, 1u);
    ASSERT_EQ(surface.context.drawn_rects.size(), 1u);
}

TEST(RenderExecutor, SubmittingANullSceneIsRefused) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    // act
    const bool submitted = executor.submit(surface, nullptr);
    // assert
    EXPECT_FALSE(submitted);
    EXPECT_EQ(executor.executed_count(), 0u);
}

TEST(RenderExecutor, AnOlderPendingFrameIsDropped) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    // act
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f)));
    executor.flush();
    // assert
    EXPECT_EQ(executor.executed_count(), 1u);
}

TEST(RenderExecutor, FlushReturnsOnlyOnceNothingIsPending) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
    // act
    executor.flush();
    // assert
    EXPECT_FALSE(executor.pending());
    EXPECT_EQ(executor.executed_count(), 1u);
}

TEST(RenderExecutor, ManyFramesAllExecute) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // act
    for (int index = 0; index < 20; index++) {
        executor.submit(surface, scene);
        executor.flush();
    }
    // assert
    EXPECT_EQ(executor.executed_count(), 20u);
}

TEST(RenderExecutor, AStoppedExecutorAcceptsNothing) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    // act
    executor.stop();
    const bool submitted = executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
    // assert
    EXPECT_FALSE(submitted);
}

TEST(RenderExecutor, StoppingTwiceIsHarmless) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    // act
    executor.stop();
    executor.stop();
    // assert
    EXPECT_EQ(executor.executed_count(), 0u);
}

TEST(RenderExecutor, StoppingFlushesTheFrameAlreadyQueued) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
    executor.flush();
    // act
    executor.stop();
    // assert
    EXPECT_EQ(executor.executed_count(), 1u);
}

TEST(RenderExecutor, DestructionFlushesThePendingFrame) {
    // arrange
    TestRendererBackend backend;
    TestRenderSurface surface;
    const std::uint64_t executed = [&] {
        // the executor must outlive the submit, so it is scoped here and read after the scope
        RenderExecutor executor(&backend);
        executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
        return executor.executed_count();
    }();
    // act, assert
    EXPECT_EQ(executed, 0u);
    EXPECT_EQ(surface.ended, 1u);
}

TEST(RenderExecutor, DestructionStopsTheRenderThread) {
    // arrange
    TestRendererBackend backend;
    {
        RenderExecutor executor(&backend);
        EXPECT_EQ(executor.executed_count(), 0u);
    }
    // act, assert: the thread was joined, so reaching here proves it stopped
    EXPECT_EQ(backend.executed_rect_counts.size(), 0u);
}

TEST(RendererBackendFactory, StartsEmpty) {
    // arrange
    set_renderer_backend_factory(nullptr);
    // act, assert
    EXPECT_FALSE(static_cast<bool>(renderer_backend_factory()));
}

TEST(RendererBackendFactory, AnUninstalledBackendBuildsNothing) {
    // arrange
    set_renderer_backend_factory(nullptr);
    // act
    std::unique_ptr<RendererBackend> built = create_renderer_backend();
    // assert
    EXPECT_EQ(built, nullptr);
}

TEST(RendererBackendFactory, TheInstalledFactoryBuildsTheBackend) {
    // arrange
    set_renderer_backend_factory([] { return std::unique_ptr<RendererBackend>(new TestRendererBackend()); });
    // act
    std::unique_ptr<RendererBackend> built = create_renderer_backend();
    // assert
    ASSERT_NE(built, nullptr);
    EXPECT_TRUE(built->ready());
    set_renderer_backend_factory(nullptr);
}

TEST(RendererBackendFactory, TheBackendCreatesASurface) {
    // arrange
    TestRendererBackend backend;
    int layer = 0;
    // act
    std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(&layer), WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // assert
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
}

TEST(RendererBackendFactory, TheBackendExecutesAScene) {
    // arrange
    TestRendererBackend backend;
    TestRenderSurface surface;
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // act
    backend.execute_frame(surface, scene);
    // assert
    ASSERT_EQ(backend.executed_rect_counts.size(), 1u);
    EXPECT_EQ(surface.begun, 1u);
    EXPECT_EQ(surface.ended, 1u);
}
