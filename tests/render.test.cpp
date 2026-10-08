#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/custom_render_node.h"
#include "render/frame_scene.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"
#include "render/resource_factory.h"

using namespace albor;

namespace
{
    // a shaped run standing in for what a text engine produced, so a recorder test draws without a font backend (§9.1)
    std::shared_ptr<const ShapedRun> synthetic_run(std::size_t glyph_count, float font_size) {
        // the line is built aside, because the run stands for one unwrapped line (§9.1)
        ShapedRun::Line line;
        // the line origin is the baseline the synthesized glyphs hang from (§9.1)
        line.origin = Point{.x = 0.0f, .y = 12.0f};
        // every glyph contributes a distinct index and a position that grows with it (§9.1)
        for (std::size_t index = 0; index < glyph_count; index++)
            line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(index), .position = Point{.x = static_cast<float>(index) * 8.0f, .y = 0.0f}});
        // the run holds that one line, which is what an unwrapped run shapes into (§9.1)
        return ShapedRun::make({std::move(line)}, font_size);
    }

    // a measured run standing in for what a text engine reports, so a test asserts on extents without a font backend (§9.1)
    TextLayout synthetic_layout(std::size_t glyph_count, float font_size, Size size) {
        // the shaped run travels beside the extents the layout reports, which is what a measurement hands over (§9.1)
        return TextLayout{.run = synthetic_run(glyph_count, font_size), .size = size, .ascent = font_size * 0.8f, .descent = font_size * 0.2f};
    }

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

        // the shaped runs the surface was asked to draw, in order (§9.1)
        std::vector<std::shared_ptr<const ShapedRun>> drawn_texts;

        // the retained line batches the surface was asked to draw, in order
        std::vector<std::shared_ptr<const LineBatch>> drawn_batches;

        // the transform of every drawn line batch, parallel to the batches
        std::vector<Transform> drawn_batch_transforms;

        // the stroke of every drawn line batch, parallel to the batches
        std::vector<StrokeStyle> drawn_batch_strokes;

        // the retained paths the surface was asked to draw, in order
        std::vector<std::shared_ptr<const PathResource>> drawn_paths;

        // the transform of every drawn path, parallel to the paths
        std::vector<Transform> drawn_path_transforms;

        // the paint of every drawn path, parallel to the paths
        std::vector<Paint> drawn_path_paints;

        // the retained pictures the surface was asked to replay, in order
        std::vector<std::shared_ptr<const PictureResource>> drawn_pictures;

        // the transform of every drawn picture, parallel to the pictures
        std::vector<Transform> drawn_picture_transforms;

        // the retained images the surface was asked to draw, in order
        std::vector<std::shared_ptr<const ImageResource>> drawn_images;

        // the destination of every drawn image, parallel to the images
        std::vector<Rect> drawn_image_destinations;

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

        void draw_text(const TextLayout& layout, Point, const Paint&) override { drawn_texts.push_back(layout.run); }

        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override {
            drawn_batches.push_back(batch);
            drawn_batch_transforms.push_back(transform);
            drawn_batch_strokes.push_back(stroke);
        }

        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override {
            drawn_paths.push_back(path);
            drawn_path_transforms.push_back(transform);
            drawn_path_paints.push_back(paint);
        }

        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override {
            drawn_pictures.push_back(picture);
            drawn_picture_transforms.push_back(transform);
        }

        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint&) override {
            drawn_images.push_back(image);
            drawn_image_destinations.push_back(destination);
        }

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

        // how many begun frames were presented
        std::size_t presented = 0;

        // whether a frame began and has not been ended yet
        bool frame_in_flight = false;

        // whether the surface accepts a frame at all
        bool is_valid = true;

        // the metrics the surface presents at, which a resize changes
        WindowMetrics surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);

        [[nodiscard]] bool valid() const override { return is_valid; }

        [[nodiscard]] const WindowMetrics& metrics() const override { return surface_metrics; }

        void resize(const WindowMetrics& metrics) override { surface_metrics = metrics; }

        RenderContext& begin_frame() override {
            // the begin registers the frame state that the matching end will present (§6.1)
            begun++;
            frame_in_flight = true;
            return context;
        }

        void end_frame() override {
            ended++;
            // the end presents the frame the begin registered, so an ended frame without one presents nothing (§6.3)
            if (!frame_in_flight)
                return;
            presented++;
            frame_in_flight = false;
        }
    };

    // the backend test double, which records the scenes stage two executes (§6.2)
    class TestRendererBackend final : public RendererBackend
    {
    public:
        // the rectangles of every scene the backend was asked to execute, in order
        std::vector<std::size_t> executed_rect_counts;

        // the number of retained line batches every scene carried, in order (§6.4, AD-06)
        std::vector<std::size_t> executed_batch_counts;

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
            for (const LineBatchDraw& retained : scene.line_batches())
                context.draw_line_batch(retained.batch, retained.transform, retained.stroke);
            context.restore();
            surface.end_frame();
            executed_rect_counts.push_back(scene.rects().size());
            executed_batch_counts.push_back(scene.line_batches().size());
        }
    };

    // the backend double that parks inside execute_frame until the test releases it, which pins the render thread inside one frame (§6.1)
    class GatedRendererBackend final : public RendererBackend
    {
    public:
        // the logical widths of every scene that reached the backend, which identifies which frame survived (§6.1)
        std::vector<float> executed_widths;

        [[nodiscard]] bool ready() const override { return true; }

        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle, const WindowMetrics&) override { return std::make_unique<TestRenderSurface>(); }

        void resize_surface(RenderSurface&, const WindowMetrics&) override {}

        void execute_frame(RenderSurface&, const FrameScene& scene) override {
            std::unique_lock<std::mutex> lock(m_mutex);
            // the render thread parks here, so the ui thread submits into a known in flight state (§6.1)
            m_arrived++;
            m_arrived_condition.notify_all();
            m_gate.wait(lock, [this] { return m_released; });
            executed_widths.push_back(scene.metrics().logical_size.width);
        }

        // wait until the render thread is parked inside a frame, which makes the following submit deterministic (§6.1)
        void wait_until_arrived() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_arrived_condition.wait(lock, [this] { return m_arrived > 0; });
        }

        // let the parked render thread finish its frame (§6.1)
        void release() {
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_released = true;
            }
            m_gate.notify_all();
        }

        ~GatedRendererBackend() override { release(); }

    private:
        // the lock the gate and the record share
        std::mutex m_mutex;

        // the condition the parked render thread waits on (§6.1)
        std::condition_variable m_gate;

        // the condition the test waits on until the render thread parked (§6.1)
        std::condition_variable m_arrived_condition;

        // how many frames reached the gate, which tells the test the render thread is parked (§6.1)
        std::size_t m_arrived = 0;

        // whether the test released the parked render thread (§6.1)
        bool m_released = false;
    };

    // the custom node test double, which paints one rectangle through its retained content hook (§6.4)
    class TestCustomNode final : public CustomRenderNode
    {
    public:
        // how many times the retained content was requested (§6.4)
        std::size_t renders = 0;

        // the bounds of the last retained content request (§6.4)
        Rect last_bounds;

        void render_content(RenderContext& context, const Rect& bounds) override {
            // the visual of the node is one rectangle the size of its bounds (§6.4)
            renders++;
            last_bounds = bounds;
            context.draw_rect(bounds, Paint{});
        }
    };
} // namespace

TEST(FontHandle, ADefaultHandleNamesNoTypeface) {
    // arrange, act
    const FontHandle handle;
    // assert a handle with nothing in it names no typeface, so a backend skips the run rather than guessing (§9.1)
    EXPECT_FALSE(handle.valid());
}

TEST(FontHandle, AWrappedTypefaceIsAValidHandle) {
    // arrange
    int typeface = 0;
    // act
    const FontHandle handle = FontHandle::from_object(&typeface);
    // assert wrapping a resolved typeface is what makes the handle usable (§9.1)
    EXPECT_TRUE(handle.valid());
}

TEST(FontHandle, TheWrappedTypefaceLeavesAsAnOpaquePointer) {
    // arrange
    int typeface = 0;
    // act
    const FontHandle handle = FontHandle::from_object(&typeface);
    // assert the typeface leaves the handle untyped, so no shaping type reaches a public header (§3.5, §9.1)
    EXPECT_EQ(handle.object(), static_cast<const void*>(&typeface));
}

TEST(FontHandle, AHandleOverNothingHandsBackNothing) {
    // arrange, act
    const FontHandle handle;
    // assert an invalid handle carries no object at all (§9.1)
    EXPECT_EQ(handle.object(), nullptr);
}

TEST(FontHandle, EqualityComparesTheWrappedTypeface) {
    // arrange
    int typeface = 0;
    // act
    const FontHandle wrapped = FontHandle::from_object(&typeface);
    const FontHandle same = FontHandle::from_object(&typeface);
    const FontHandle other = FontHandle::from_object(nullptr);
    // assert two handles are the same typeface or they are not (§9.1)
    EXPECT_EQ(wrapped, same);
    EXPECT_NE(wrapped, other);
}

TEST(ShapedRun, ARunOverNoLinesIsEmpty) {
    // arrange, act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({}, 16.0f);
    // assert a run that shaped no line carries no glyph (§6.2)
    EXPECT_TRUE(run->empty());
}

TEST(ShapedRun, ARunOverLinesWithNoGlyphsIsEmpty) {
    // arrange
    ShapedRun::Line line;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a line that shaped no glyph still costs nothing to draw (§6.2)
    EXPECT_TRUE(run->empty());
}

TEST(ShapedRun, ARunOverGlyphsIsNotEmpty) {
    // arrange, act
    const std::shared_ptr<const ShapedRun> run = synthetic_run(3, 16.0f);
    // assert a run that shaped glyphs is what the render stage replays (§9.1)
    EXPECT_FALSE(run->empty());
}

TEST(ShapedRun, TheGlyphCountSumsTheLines) {
    // arrange
    ShapedRun::Line first;
    first.glyphs.push_back(ShapedGlyph{.id = 1});
    first.glyphs.push_back(ShapedGlyph{.id = 2});
    ShapedRun::Line second;
    second.glyphs.push_back(ShapedGlyph{.id = 3});
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({first, second}, 16.0f);
    // assert a wrapped run reports every glyph of every line, which is what a reservation is sized from (§9.1)
    EXPECT_EQ(run->glyph_count(), 3u);
}

TEST(ShapedRun, TheFontSizeIsTheOneEveryLineWasShapedAt) {
    // arrange, act
    const std::shared_ptr<const ShapedRun> run = synthetic_run(1, 22.0f);
    // assert the renderer builds its font from this size rather than from the run height, which is what fixes a wrapped run (§9.1)
    EXPECT_FLOAT_EQ(run->font_size(), 22.0f);
}

TEST(ShapedRun, TheLinesAreKeptInReadingOrder) {
    // arrange, act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({ShapedRun::Line{}, ShapedRun::Line{}, ShapedRun::Line{}}, 16.0f);
    // assert a renderer replays the lines in the order they were shaped (§9.1)
    ASSERT_EQ(run->lines().size(), 3u);
}

TEST(ShapedRun, EveryLineCarriesItsOwnBaseline) {
    // arrange
    ShapedRun::Line first;
    first.origin = Point{.x = 0.0f, .y = 12.0f};
    ShapedRun::Line second;
    second.origin = Point{.x = 0.0f, .y = 28.0f};
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({first, second}, 16.0f);
    // assert a wrapped run places each line on its own baseline, which is what keeps the lines apart (§9.1)
    ASSERT_EQ(run->lines().size(), 2u);
    EXPECT_FLOAT_EQ(run->lines()[0].origin.y, 12.0f);
    EXPECT_FLOAT_EQ(run->lines()[1].origin.y, 28.0f);
}

TEST(ShapedRun, EveryLineCarriesItsOwnTypeface) {
    // arrange
    int typeface = 0;
    ShapedRun::Line shaped;
    shaped.font = FontHandle::from_object(&typeface);
    ShapedRun::Line fallback;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({shaped, fallback}, 16.0f);
    // assert a line the engine could not cover carries no typeface, so a backend skips it instead of drawing a wrong font (§9.1)
    ASSERT_EQ(run->lines().size(), 2u);
    EXPECT_TRUE(run->lines()[0].font.valid());
    EXPECT_FALSE(run->lines()[1].font.valid());
}

TEST(ShapedRun, EqualityComparesTheGlyphsAndTheSize) {
    // arrange
    const std::shared_ptr<const ShapedRun> first = synthetic_run(2, 16.0f);
    // act
    const std::shared_ptr<const ShapedRun> same = synthetic_run(2, 16.0f);
    const std::shared_ptr<const ShapedRun> other_size = synthetic_run(2, 24.0f);
    const std::shared_ptr<const ShapedRun> other_glyphs = synthetic_run(3, 16.0f);
    // assert two runs are equal only when they place the same glyphs at the same size (§9.1)
    EXPECT_EQ(*first, *same);
    EXPECT_NE(*first, *other_size);
    EXPECT_NE(*first, *other_glyphs);
}

TEST(TextLayout, AnUnmeasuredLayoutIsEmpty) {
    // arrange, act
    const TextLayout layout;
    // assert a layout that measured nothing draws nothing (§6.2)
    EXPECT_TRUE(layout.empty());
}

TEST(TextLayout, ALayoutCarryingAShapedRunIsNotEmpty) {
    // arrange, act
    const TextLayout layout = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    // assert the shaped run is what makes the layout drawable (§9.1)
    EXPECT_FALSE(layout.empty());
}

TEST(TextLayout, ALayoutWhoseRunCarriesNoGlyphIsEmpty) {
    // arrange
    TextLayout layout;
    layout.run = synthetic_run(0, 16.0f);
    // act, assert a run with no glyphs costs nothing to draw, so the scene skips it (§6.2)
    EXPECT_TRUE(layout.empty());
}

TEST(TextLayout, TheLayoutCarriesTheShapedRun) {
    // arrange, act
    const TextLayout layout = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    // assert the run travels beside the extents, so stage two replays what stage one measured (§9.1, AD-18)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_EQ(layout.run->glyph_count(), 5u);
}

TEST(TextLayout, TheExtentsAreReportedBesideTheRun) {
    // arrange, act
    const TextLayout layout = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    // assert a container sizes itself from the extents the measurement reported (§8.2)
    EXPECT_FLOAT_EQ(layout.size.width, 80.0f);
    EXPECT_FLOAT_EQ(layout.size.height, 16.0f);
    EXPECT_GT(layout.ascent, 0.0f);
    EXPECT_GT(layout.descent, 0.0f);
}

TEST(TextLayout, EqualityComparesWhatTheLayoutReports) {
    // arrange
    const TextLayout first = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    const TextLayout same = synthetic_layout(4, 16.0f, Size{80.0f, 16.0f});
    const TextLayout other = synthetic_layout(5, 16.0f, Size{90.0f, 16.0f});
    // act, assert a layout is a measurement, so two layouts that report the same extent are equal (§9.1)
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}

TEST(TextLayout, EqualityTellsAnEmptyLayoutFromAMeasuredOne) {
    // arrange
    const TextLayout empty_layout;
    // act
    const TextLayout measured = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    // assert a layout that measures nothing is not the same measurement as one that does (§9.1)
    EXPECT_NE(empty_layout, measured);
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

TEST(FrameScene, ATextColourListThatDoesNotMatchTheRunsIsRefused) {
    // arrange
    FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), {});
    scene.add_text(synthetic_layout(5, 16.0f, Size{80.0f, 16.0f}), Point{10.0f, 20.0f});
    // act
    scene.set_text_colors({Color::rgb(1.0f, 0.0f, 0.0f), Color::rgb(0.0f, 1.0f, 0.0f)});
    // assert
    EXPECT_TRUE(scene.text_colors().empty());
}

TEST(FrameScene, ASetTextColourTravelsWithTheRun) {
    // arrange
    FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), {});
    scene.add_text(synthetic_layout(5, 16.0f, Size{80.0f, 16.0f}), Point{10.0f, 20.0f});
    // act
    scene.set_text_colors({Color::rgb(0.25f, 0.5f, 0.75f)});
    // assert
    ASSERT_EQ(scene.text_colors().size(), 1u);
    EXPECT_EQ(scene.text_colors()[0], (Color::rgb(0.25f, 0.5f, 0.75f)));
}

TEST(FrameScene, ARectColourListThatDoesNotMatchTheRectanglesIsRefused) {
    // arrange
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // act
    scene.set_rect_colors({Color::rgb(1.0f, 0.0f, 0.0f), Color::rgb(0.0f, 1.0f, 0.0f)});
    // assert
    EXPECT_TRUE(scene.rect_colors().empty());
}

TEST(FrameScene, ASetRectColourTravelsWithTheRectangle) {
    // arrange
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // act
    scene.set_rect_colors({Color::rgb(0.3f, 0.4f, 0.5f)});
    // assert
    ASSERT_EQ(scene.rect_colors().size(), 1u);
    EXPECT_EQ(scene.rect_colors()[0], (Color::rgb(0.3f, 0.4f, 0.5f)));
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
    const TextLayout layout = synthetic_layout(5, 16.0f, Size{80.0f, 16.0f});
    // act
    context.draw_text(layout, Point{0.0f, 0.0f}, Paint{});
    // assert the surface is handed the shaped run itself, so the draw replays what was measured (§9.1)
    ASSERT_EQ(context.drawn_texts.size(), 1u);
    EXPECT_EQ(context.drawn_texts[0], layout.run);
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

TEST(RenderSurface, EndingTheFramePresentsItExactlyOnce) {
    // arrange
    TestRenderSurface surface;
    // act
    surface.begin_frame();
    surface.end_frame();
    // assert
    EXPECT_EQ(surface.begun, 1u);
    EXPECT_EQ(surface.ended, 1u);
    EXPECT_EQ(surface.presented, 1u);
}

TEST(RenderSurface, ABegunFrameIsOnlyPresentedWhenItEnds) {
    // arrange
    TestRenderSurface surface;
    // act
    surface.begin_frame();
    surface.end_frame();
    // the surface keeps one current frame, so the second begin replaces the in flight one (§6.1)
    surface.begin_frame();
    // assert
    EXPECT_EQ(surface.presented, 1u);
    EXPECT_EQ(surface.begun, 2u);
    EXPECT_EQ(surface.ended, 1u);
}

TEST(RenderSurface, EndingWithoutBeginningPresentsNothing) {
    // arrange
    TestRenderSurface surface;
    // act
    surface.end_frame();
    // assert
    EXPECT_EQ(surface.ended, 1u);
    EXPECT_EQ(surface.presented, 0u);
}

TEST(RenderSurface, ManyFrameCyclesPresentEachOnce) {
    // arrange
    TestRenderSurface surface;
    // act
    for (int frame = 0; frame < 5; frame++) {
        surface.begin_frame();
        surface.end_frame();
    }
    // assert
    EXPECT_EQ(surface.begun, 5u);
    EXPECT_EQ(surface.ended, 5u);
    EXPECT_EQ(surface.presented, 5u);
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

TEST(RenderExecutor, TheBackendReplaysRetainedDraws) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    FrameScene built(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    built.add_line_batch(LineBatchDraw{.batch = batch, .transform = Transform{}, .stroke = StrokeStyle{}});
    const auto scene = std::make_shared<const FrameScene>(std::move(built));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert
    EXPECT_EQ(executor.executed_count(), 1u);
    ASSERT_EQ(backend.executed_batch_counts.size(), 1u);
    EXPECT_EQ(backend.executed_batch_counts[0], 1u);
    ASSERT_EQ(surface.context.drawn_batches.size(), 1u);
    EXPECT_EQ(surface.context.drawn_batches[0], batch);
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
    GatedRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    // the render thread parks inside the first frame, so the second submit lands on a known in flight state (§6.1)
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f)));
    backend.wait_until_arrived();
    // act
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f)));
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 1.0f)));
    backend.release();
    executor.flush();
    // assert
    EXPECT_EQ(executor.executed_count(), 2u);
    // the middle frame is dropped, so the render thread drew the first and the newest (§6.1)
    ASSERT_EQ(backend.executed_widths.size(), 2u);
    EXPECT_FLOAT_EQ(backend.executed_widths[0], 640.0f);
    EXPECT_FLOAT_EQ(backend.executed_widths[1], 1024.0f);
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

TEST(RenderExecutor, EveryExecutedFrameIsPresentedOnce) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // act
    for (int frame = 0; frame < 8; frame++) {
        executor.submit(surface, scene);
        executor.flush();
    }
    // assert
    EXPECT_EQ(executor.executed_count(), 8u);
    EXPECT_EQ(surface.begun, 8u);
    EXPECT_EQ(surface.ended, 8u);
    // a frame that executed always ends in a presentation, which is the §6.3 present contract
    EXPECT_EQ(surface.presented, 8u);
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

TEST(LineBatch, AFactoryBatchCarriesItsIdentifierAndSegments) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {20.0f, 0.0f}, {30.0f, 10.0f}, {40.0f, 0.0f}, {50.0f, 10.0f}};
    // act
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    // assert
    ASSERT_NE(batch, nullptr);
    EXPECT_GE(batch->id(), 1u);
    EXPECT_EQ(batch->point_count(), 6u);
    EXPECT_EQ(batch->segment_count(), 3u);
    ASSERT_EQ(batch->points().size(), 6u);
    EXPECT_EQ(batch->points()[0], (Point{0.0f, 0.0f}));
    EXPECT_EQ(batch->points()[5], (Point{50.0f, 10.0f}));
}

TEST(LineBatch, ADanglingPointIsRefused) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {20.0f, 0.0f}};
    // act
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    // assert
    EXPECT_EQ(batch, nullptr);
}

TEST(LineBatch, AnEmptyBatchIsRefused) {
    // arrange, act
    ResourceFactory factory;
    // assert
    EXPECT_EQ(factory.create_line_batch(std::span<const Point>()), nullptr);
}

TEST(PathResource, AFactoryPathCarriesItsVerbsAndPoints) {
    // arrange
    ResourceFactory factory;
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {10.0f, 0.0f}};
    // act
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, points);
    // assert
    ASSERT_NE(path, nullptr);
    EXPECT_GE(path->id(), 1u);
    ASSERT_EQ(path->verbs().size(), 4u);
    EXPECT_EQ(path->verbs()[0], PathVerb::MoveTo);
    EXPECT_EQ(path->verbs()[3], PathVerb::Close);
    ASSERT_EQ(path->points().size(), 3u);
    EXPECT_EQ(path->points()[2], (Point{10.0f, 0.0f}));
}

TEST(PathResource, AnEmptyCommandStreamIsRefused) {
    // arrange, act
    ResourceFactory factory;
    // assert
    EXPECT_EQ(factory.create_path(std::span<const PathVerb>(), std::span<const Point>()), nullptr);
}

TEST(PathResource, ACommandStreamThatMissesItsPointsIsRefused) {
    // arrange
    ResourceFactory factory;
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point too_few[] = {{0.0f, 0.0f}};
    const Point too_many[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {5.0f, 5.0f}};
    // act, assert
    EXPECT_EQ(factory.create_path(verbs, too_few), nullptr);
    EXPECT_EQ(factory.create_path(verbs, too_many), nullptr);
}

TEST(PathResource, PointDemandCountsTheConsumingVerbs) {
    // arrange
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::Close, PathVerb::LineTo, PathVerb::Close};
    // act
    const std::size_t demand = PathResource::point_demand(verbs);
    // assert
    EXPECT_EQ(demand, 3u);
}

TEST(ImageResource, AFactoryImageCarriesExtentFormatAndBytes) {
    // arrange
    ResourceFactory factory;
    const std::uint8_t pixels[] = {255, 0, 0, 255, 0, 255, 0, 255};
    // act
    const std::shared_ptr<const ImageResource> image = factory.create_image(2, 1, ImageFormat::RGBA8, pixels);
    // assert
    ASSERT_NE(image, nullptr);
    EXPECT_GE(image->id(), 1u);
    EXPECT_EQ(image->width(), 2u);
    EXPECT_EQ(image->height(), 1u);
    EXPECT_EQ(image->format(), ImageFormat::RGBA8);
    EXPECT_EQ(image->row_bytes(), 8u);
    ASSERT_EQ(image->pixels().size(), 8u);
    EXPECT_EQ(image->pixels()[1], 0u);
}

TEST(ImageResource, AnEmptyExtentIsRefused) {
    // arrange, act
    ResourceFactory factory;
    // assert
    EXPECT_EQ(factory.create_image(0, 1, ImageFormat::RGBA8, std::span<const std::uint8_t>()), nullptr);
    EXPECT_EQ(factory.create_image(2, 0, ImageFormat::RGBA8, std::span<const std::uint8_t>()), nullptr);
}

TEST(ImageResource, AMismatchedPixelBufferIsRefused) {
    // arrange
    ResourceFactory factory;
    const std::uint8_t too_few[] = {255, 0, 0, 255};
    // act
    const std::shared_ptr<const ImageResource> image = factory.create_image(2, 1, ImageFormat::RGBA8, too_few);
    // assert
    EXPECT_EQ(image, nullptr);
}

TEST(ImageResource, BytesPerPixelAndByteCountAgree) {
    // arrange, act
    const std::size_t bytes = ImageResource::byte_count(2, 3, ImageFormat::RGBA8);
    // assert
    EXPECT_EQ(ImageResource::bytes_per_pixel(ImageFormat::RGBA8), 4u);
    EXPECT_EQ(bytes, 24u);
    EXPECT_EQ(ImageResource::byte_count(0, 3, ImageFormat::RGBA8), (std::numeric_limits<std::size_t>::max)());
}

TEST(PictureRecorder, CommandsAreRecordedInOrder) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    recorder.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 4.0f, Paint{});
    recorder.draw_text(synthetic_layout(5, 16.0f, Size{80.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{});
    // assert
    ASSERT_EQ(recorder.commands().size(), 3u);
    EXPECT_EQ(recorder.commands()[0].kind, PictureCommandKind::Rect);
    EXPECT_EQ(recorder.commands()[1].kind, PictureCommandKind::RoundedRect);
    EXPECT_EQ(recorder.commands()[2].kind, PictureCommandKind::Text);
    EXPECT_FLOAT_EQ(recorder.commands()[1].radius, 4.0f);
}

TEST(PictureRecorder, AnEmptyRectangleDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{5.0f, 5.0f}, Size{0.0f, 10.0f}), Paint{});
    recorder.draw_text(synthetic_layout(0, 16.0f, Size{0.0f, 0.0f}), Point{0.0f, 0.0f}, Paint{});
    // assert a run with no glyphs records nothing, because it would draw nothing (§6.2)
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, ACommandStampsTheTransformItWasRecordedUnder) {
    // arrange
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(5.0f, 6.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].transform, (Transform::translation(5.0f, 6.0f)));
}

TEST(PictureRecorder, SaveAndRestoreRoundTripTheTransform) {
    // arrange
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(5.0f, 6.0f));
    recorder.save();
    recorder.set_transform(Transform::scaling(2.0f, 2.0f));
    // act
    recorder.restore();
    // assert
    EXPECT_EQ(recorder.transform(), (Transform::translation(5.0f, 6.0f)));
}

TEST(PictureRecorder, ABatchCommandCarriesItsRetainedBatch) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    PictureRecorder recorder;
    StrokeStyle stroke;
    stroke.color = Color::rgb(0.9f, 0.1f, 0.2f);
    // act
    recorder.draw_line_batch(batch, Transform::translation(10.0f, 20.0f), stroke);
    // assert
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].kind, PictureCommandKind::LineBatch);
    EXPECT_EQ(recorder.commands()[0].batch, batch);
    EXPECT_EQ(recorder.commands()[0].transform, (Transform::translation(10.0f, 20.0f)));
    EXPECT_EQ(recorder.commands()[0].stroke, stroke);
}

TEST(PictureRecorder, AnImageCommandCarriesItsRetainedImage) {
    // arrange
    ResourceFactory factory;
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> image = factory.create_image(1, 1, ImageFormat::RGBA8, pixels);
    PictureRecorder recorder;
    // act
    recorder.draw_image(image, Rect::from_origin_size(Point{10.0f, 20.0f}, Size{4.0f, 2.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].kind, PictureCommandKind::Image);
    EXPECT_EQ(recorder.commands()[0].image, image);
    EXPECT_FLOAT_EQ(recorder.commands()[0].rect.x, 10.0f);
}

TEST(PictureRecorder, ANestedPictureFlattensInOrder) {
    // arrange
    PictureRecorder inner;
    inner.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{5.0f, 5.0f}), Paint{});
    ResourceFactory factory;
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(inner);
    PictureRecorder outer;
    outer.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    // act
    outer.draw_picture(picture, Transform{});
    outer.draw_text(synthetic_layout(5, 16.0f, Size{80.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{});
    // assert
    ASSERT_EQ(outer.commands().size(), 3u);
    EXPECT_EQ(outer.commands()[0].kind, PictureCommandKind::Rect);
    EXPECT_EQ(outer.commands()[1].kind, PictureCommandKind::Rect);
    EXPECT_EQ(outer.commands()[2].kind, PictureCommandKind::Text);
}

TEST(PictureRecorder, AFactoryPictureRefusesAnEmptyRecording) {
    // arrange
    ResourceFactory factory;
    PictureRecorder recorder;
    // act
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    // assert
    EXPECT_EQ(picture, nullptr);
}

TEST(PictureRecorder, ResetDropsTheRecordedCommands) {
    // arrange
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    // act
    recorder.reset();
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(ResourceFactory, EveryResourceGetsAUniqueIdentifier) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point path_points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    // act
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, path_points);
    const std::shared_ptr<const ImageResource> image = factory.create_image(1, 1, ImageFormat::RGBA8, pixels);
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{1.0f, 1.0f}), Paint{});
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    // assert
    ASSERT_NE(batch, nullptr);
    ASSERT_NE(path, nullptr);
    ASSERT_NE(image, nullptr);
    ASSERT_NE(picture, nullptr);
    const std::size_t ids[] = {static_cast<std::size_t>(batch->id()), static_cast<std::size_t>(path->id()), static_cast<std::size_t>(image->id()), static_cast<std::size_t>(picture->id())};
    for (std::size_t left = 0; left < 4; left++)
        for (std::size_t right = left + 1; right < 4; right++)
            EXPECT_NE(ids[left], ids[right]);
}

TEST(FrameScene, RetainedDrawsReplayInRecordingOrder) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point path_points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, path_points);
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> image = factory.create_image(1, 1, ImageFormat::RGBA8, pixels);
    PictureRecorder picture_recorder;
    picture_recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{1.0f, 1.0f}), Paint{});
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(picture_recorder);
    FrameScene scene(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    const Rect destination = Rect::from_origin_size(Point{2.0f, 3.0f}, Size{4.0f, 4.0f});
    const Transform transform = Transform::translation(7.0f, 8.0f);
    StrokeStyle stroke;
    stroke.width = 2.0f;
    // act
    scene.add_line_batch(LineBatchDraw{.batch = batch, .transform = transform, .stroke = stroke});
    scene.add_path(PathDraw{.path = path, .transform = transform, .paint = Paint{}});
    scene.add_picture(PictureDraw{.picture = picture, .transform = transform});
    scene.add_image(ImageDraw{.image = image, .destination = destination});
    // assert
    ASSERT_EQ(scene.line_batches().size(), 1u);
    EXPECT_EQ(scene.line_batches()[0].batch, batch);
    EXPECT_EQ(scene.line_batches()[0].stroke, stroke);
    ASSERT_EQ(scene.paths().size(), 1u);
    EXPECT_EQ(scene.paths()[0].path, path);
    ASSERT_EQ(scene.pictures().size(), 1u);
    EXPECT_EQ(scene.pictures()[0].picture, picture);
    ASSERT_EQ(scene.images().size(), 1u);
    EXPECT_EQ(scene.images()[0].image, image);
    EXPECT_EQ(scene.images()[0].destination, destination);
}

TEST(FrameScene, ANullRetainedDrawIsRefused) {
    // arrange, act
    FrameScene scene(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    scene.add_line_batch(LineBatchDraw{.batch = nullptr, .transform = Transform{}, .stroke = StrokeStyle{}});
    scene.add_path(PathDraw{.path = nullptr, .transform = Transform{}, .paint = Paint{}});
    scene.add_picture(PictureDraw{.picture = nullptr, .transform = Transform{}});
    scene.add_image(ImageDraw{.image = nullptr, .destination = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f})});
    // assert
    EXPECT_TRUE(scene.line_batches().empty());
    EXPECT_TRUE(scene.paths().empty());
    EXPECT_TRUE(scene.pictures().empty());
    EXPECT_TRUE(scene.images().empty());
}

TEST(CustomRenderNode, MeasureConstrainsTheIntrinsicSize) {
    // arrange
    TestCustomNode node;
    // act
    Size measured{};
    node.measure(SizeConstraints::at_most(Size{50.0f, 200.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 50.0f);
    EXPECT_FLOAT_EQ(measured.height, 100.0f);
}

TEST(CustomRenderNode, AnIntrinsicSizeIsReplaced) {
    // arrange
    TestCustomNode node;
    node.set_intrinsic_size(Size{30.0f, 40.0f});
    // act
    Size measured{};
    node.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_EQ(node.intrinsic_size(), (Size{30.0f, 40.0f}));
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(CustomRenderNode, ARenderReachesTheRetainedHook) {
    // arrange
    TestCustomNode node;
    node.set_intrinsic_size(Size{30.0f, 40.0f});
    const Rect bounds = Rect::from_origin_size(Point{5.0f, 6.0f}, Size{30.0f, 40.0f});
    node.arrange(bounds);
    TestRenderContext context;
    // act
    node.render(context);
    // assert
    EXPECT_EQ(node.renders, 1u);
    EXPECT_EQ(node.last_bounds, bounds);
    ASSERT_EQ(context.drawn_rects.size(), 1u);
    EXPECT_EQ(context.drawn_rects[0], bounds);
}

TEST(CustomRenderNode, AnInvisibleNodeDoesNotRender) {
    // arrange
    TestCustomNode node;
    node.set_visible(false);
    TestRenderContext context;
    // act
    node.render(context);
    // assert
    EXPECT_EQ(node.renders, 0u);
}

TEST(CustomRenderNode, ANodeWithoutAreaDoesNotRender) {
    // arrange
    TestCustomNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 0.0f}));
    TestRenderContext context;
    // act
    node.render(context);
    // assert
    EXPECT_EQ(node.renders, 0u);
}

TEST(CustomRenderNode, AnUnchangedIntrinsicSizeInvalidatesNothing) {
    // arrange
    TestCustomNode node;
    node.clear_dirty();
    // act
    node.set_intrinsic_size(Size{100.0f, 100.0f});
    // assert
    EXPECT_FALSE(node.layout_dirty());
    // act
    node.set_intrinsic_size(Size{64.0f, 64.0f});
    // assert
    EXPECT_TRUE(node.layout_dirty());
}

TEST(PictureRecorder, ACommandCountReflectsTheRecording) {
    // arrange
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}), Paint{});
    ResourceFactory factory;
    // act
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    // assert
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->command_count(), 2u);
    EXPECT_EQ(picture->command_count(), picture->commands().size());
    EXPECT_FALSE(picture->empty());
}

TEST(PictureRecorder, RestoreWithoutASavedStateKeepsTheTransform) {
    // arrange, act
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(5.0f, 6.0f));
    recorder.restore();
    // assert
    EXPECT_EQ(recorder.transform(), (Transform::translation(5.0f, 6.0f)));
}

TEST(PictureRecorder, EachClipKindStoresItsRectangle) {
    // arrange
    PictureRecorder recorder;
    // act
    recorder.clip_rect(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}));
    const Rect first_clip = recorder.clip();
    recorder.clip_rounded_rect(Rect::from_origin_size(Point{4.0f, 5.0f}, Size{20.0f, 20.0f}), 3.0f);
    // assert
    EXPECT_EQ(first_clip, (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f})));
    EXPECT_EQ(recorder.clip(), (Rect::from_origin_size(Point{4.0f, 5.0f}, Size{20.0f, 20.0f})));
}

TEST(PictureRecorder, AnEmptyRoundedRectDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_rounded_rect(Rect::from_origin_size(Point{5.0f, 5.0f}, Size{0.0f, 10.0f}), 2.0f, Paint{});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, ANullBatchDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_line_batch(nullptr, Transform{}, StrokeStyle{});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, APathCommandCarriesItsRetainedPath) {
    // arrange
    ResourceFactory factory;
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::Close};
    const Point path_points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}};
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, path_points);
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(5.0f, 6.0f));
    Paint paint;
    paint.fill = Color::rgb(0.2f, 0.4f, 0.6f);
    // act
    recorder.draw_path(path, Transform::scaling(2.0f, 2.0f), paint);
    // assert
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].kind, PictureCommandKind::Path);
    EXPECT_EQ(recorder.commands()[0].path, path);
    // the local transform applies first, then the recorder's translation on top (§6.4)
    EXPECT_EQ(recorder.commands()[0].transform.apply(Point{1.0f, 0.0f}), (Point{7.0f, 6.0f}));
    EXPECT_EQ(recorder.commands()[0].transform.apply(Point{0.0f, 1.0f}), (Point{5.0f, 8.0f}));
    EXPECT_EQ(recorder.commands()[0].paint, paint);
}

TEST(PictureRecorder, ANullPathDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_path(nullptr, Transform{}, Paint{});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, ANullPictureDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_picture(nullptr, Transform{});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, ANullImageOrEmptyDestinationDoesNotRecord) {
    // arrange, act
    PictureRecorder recorder;
    recorder.draw_image(nullptr, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f}), Paint{});
    EXPECT_TRUE(recorder.empty());
    recorder.draw_image(std::shared_ptr<const ImageResource>(), Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 4.0f}), Paint{});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PathResource, AnUnknownVerbSignalsUnsatisfiableDemand) {
    // arrange
    const PathVerb unknown[] = {static_cast<PathVerb>(255)};
    // act
    const std::size_t demand = PathResource::point_demand(unknown);
    // assert
    EXPECT_EQ(demand, (std::numeric_limits<std::size_t>::max)());
}

TEST(FrameScene, AnEmptyTextRunIsRefused) {
    // arrange, act
    FrameScene scene(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    scene.add_text(TextLayout{}, Point{0.0f, 0.0f});
    // assert
    EXPECT_TRUE(scene.texts().empty());
    EXPECT_TRUE(scene.text_origins().empty());
}
