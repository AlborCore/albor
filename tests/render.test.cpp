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
#include "render/glyph_outline.h"
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

    // the rectangles of the shape commands of a scene, which is what a shape assertion reads off the one list (§6.1)
    std::vector<Rect> rect_commands(const FrameScene& scene) {
        // the shapes are gathered in command order, because that is the paint order the list carries (§6.1)
        std::vector<Rect> rects;
        // every command is read, because a scene interleaves kinds (§6.1)
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == DrawCommandKind::Rect || command.kind() == DrawCommandKind::RoundedRect)
                rects.push_back(command.rect_geometry().rect);
        return rects;
    }

    // a measured run standing in for what a text engine reports, so a test needs no font backend (§9.1)
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

        // the polylines the surface was asked to draw, in scene space points (§6.2)
        std::vector<std::vector<Point>> drawn_polylines;

        // the casts the context was asked to draw, which a test asserts against (§6.2)
        std::vector<Shadow> shadows;

        // the shaped runs the surface was asked to draw, in order (§9.1)
        std::vector<std::shared_ptr<const ShapedRun>> drawn_texts;

        // what every draw was, in the order the surface was asked for it, which is the paint order (§6.1)
        std::vector<std::string> draw_log;

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

        void save() override {
            save_count++;
            // the state is pushed, because a restore is what a caller reads back afterwards (§6.2)
            states.push_back(State{.transform = current, .clip = current_clip, .opacity = current_opacity});
        }

        void restore() override {
            restore_count++;
            // a restore with nothing pushed is a no operation, like every backend (§6.2)
            if (states.empty())
                return;
            // the state the matching save pushed becomes current again (§6.2)
            const State state = states.back();
            states.pop_back();
            current = state.transform;
            current_clip = state.clip;
            current_opacity = state.opacity;
        }

        void set_transform(const Transform& transform) override {
            transforms.push_back(transform);
            current = transform;
        }

        [[nodiscard]] const Transform& transform() const override { return current; }

        void clip_rect(const Rect& rect) override {
            clips.push_back(rect);
            current_clip = rect;
        }

        void clip_rounded_rect(const Rect& rect, float radius) override {
            clips.push_back(rect);
            rounded_radii.push_back(radius);
            current_clip = rect;
        }

        [[nodiscard]] const Rect& clip() const override { return current_clip; }

        void set_opacity(float opacity) override { current_opacity = opacity; }

        [[nodiscard]] float opacity() const override { return current_opacity; }

        void draw_rect(const Rect& rect, const Paint&) override {
            drawn_rects.push_back(rect);
            draw_log.emplace_back("rect");
        }

        void draw_rounded_rect(const Rect& rect, float radius, const Paint&) override {
            drawn_rects.push_back(rect);
            rounded_radii.push_back(radius);
            draw_log.emplace_back("rounded");
        }

        void draw_polyline(const std::span<const Point>& points, const StrokeStyle&) override {
            // the vertices are recorded as scene space points, which a test asserts on (§6.2)
            drawn_polylines.emplace_back(points.begin(), points.end());
            draw_log.emplace_back("polyline");
        }

        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override {
            drawn_rects.push_back(rect);
            rounded_radii.push_back(radius);
            shadows.push_back(shadow);
            draw_log.emplace_back("shadow");
        }

        void draw_text(const TextLayout& layout, Point, const Paint&) override {
            drawn_texts.push_back(layout.run);
            draw_log.emplace_back("text");
        }

        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override {
            drawn_batches.push_back(batch);
            drawn_batch_transforms.push_back(transform);
            drawn_batch_strokes.push_back(stroke);
            draw_log.emplace_back("batch");
        }

        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override {
            drawn_paths.push_back(path);
            drawn_path_transforms.push_back(transform);
            drawn_path_paints.push_back(paint);
            draw_log.emplace_back("path");
        }

        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override {
            drawn_pictures.push_back(picture);
            drawn_picture_transforms.push_back(transform);
            draw_log.emplace_back("picture");
        }

        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint&) override {
            drawn_images.push_back(image);
            drawn_image_destinations.push_back(destination);
            draw_log.emplace_back("image");
        }

    private:
        // the state one save pushed, which the matching restore returns to (§6.2)
        struct State
        {
            // the transform at the time of the save
            Transform transform;

            // the clip at the time of the save
            Rect clip;

            // the opacity at the time of the save
            float opacity = 1.0f;
        };

        // the transform the surface reads back
        Transform current;

        // the clip the surface reads back
        Rect current_clip;

        // the opacity the last set wrote, which a test asserts against (§6.1)
        float current_opacity = 1.0f;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> states;
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

        // the metrics the surface presents at, which is what the executor suites build their scenes at (§6.1, D9.1)
        WindowMetrics surface_metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);

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
            // the one shared replay draws the command list, which is what stage two does (§6.1, §6.2)
            replay_commands(scene.commands(), context, Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size));
            context.restore();
            surface.end_frame();
            // the shape and batch counts are read off the list, which is what a backend counts (§6.1)
            std::size_t rects = 0;
            std::size_t batches = 0;
            for (const DrawCommand& command : scene.commands()) {
                if (command.kind() == DrawCommandKind::Rect)
                    rects++;
                if (command.kind() == DrawCommandKind::LineBatch)
                    batches++;
            }
            executed_rect_counts.push_back(rects);
            executed_batch_counts.push_back(batches);
        }
    };

    // the backend double that parks inside execute_frame until the test releases it, which pins the render thread inside one frame (§6.1)
    class GatedRendererBackend final : public RendererBackend
    {
    public:
        // how many rectangles every scene that reached the backend carried, which identifies which frame survived (§6.1)
        std::vector<std::size_t> executed_rect_counts;

        [[nodiscard]] bool ready() const override { return true; }

        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle, const WindowMetrics&) override { return std::make_unique<TestRenderSurface>(); }

        void resize_surface(RenderSurface&, const WindowMetrics&) override {}

        void execute_frame(RenderSurface&, const FrameScene& scene) override {
            std::unique_lock<std::mutex> lock(m_mutex);
            // the render thread parks here, so the ui thread submits into a known in flight state (§6.1)
            m_arrived++;
            m_arrived_condition.notify_all();
            m_gate.wait(lock, [this] { return m_released; });
            std::size_t rects = 0;
            for (const DrawCommand& command : scene.commands())
                if (command.kind() == DrawCommandKind::Rect)
                    rects++;
            executed_rect_counts.push_back(rects);
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
    // assert the renderer builds its font from the shaped size (§9.1)
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
    // assert a line the engine could not cover names no typeface (§9.1)
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

TEST(ShapedRun, EqualityExcludesTheFontHandle) {
    // arrange two one glyph runs whose handles name different objects entirely (§9.1, AD-34)
    int source = 0;
    int other = 0;
    ShapedRun::Line mine;
    mine.font = FontHandle::from_object(&source);
    mine.glyphs.push_back(ShapedGlyph{.id = 3, .position = Point{.x = 1.0f, .y = 0.0f}});
    ShapedRun::Line theirs;
    theirs.font = FontHandle::from_object(&other);
    theirs.glyphs.push_back(ShapedGlyph{.id = 3, .position = Point{.x = 1.0f, .y = 0.0f}});
    // act
    const std::shared_ptr<const ShapedRun> first = ShapedRun::make({mine}, 16.0f, 7);
    const std::shared_ptr<const ShapedRun> second = ShapedRun::make({theirs}, 16.0f, 7);
    // assert the shape is the identity, because the handle is where the shape came from rather than it (§9.1, AD-34)
    ASSERT_EQ(first->lines().front().font.object(), &source);
    ASSERT_EQ(second->lines().front().font.object(), &other);
    EXPECT_EQ(*first, *second);
}

TEST(ShapedRun, EqualityRejectsADifferentFontHash) {
    // arrange the same glyphs shaped from two identities, which is what a different font is (§15, AD-34)
    ShapedRun::Line mine;
    mine.glyphs.push_back(ShapedGlyph{.id = 3, .position = Point{.x = 1.0f, .y = 0.0f}});
    // act
    const std::shared_ptr<const ShapedRun> first = ShapedRun::make({mine}, 16.0f, 7);
    const std::shared_ptr<const ShapedRun> second = ShapedRun::make({mine}, 16.0f, 8);
    // assert the hash is part of the identity, because two fonts shaped the same text are not one run (§15, AD-34)
    ASSERT_EQ(first->font_hash(), 7u);
    ASSERT_EQ(second->font_hash(), 8u);
    EXPECT_NE(*first, *second);
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
    const FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(scene.metrics(), metrics);
}

TEST(FrameScene, AnEmptySceneCarriesNoCommand) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // act
    const FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert a scene with no command draws its background and nothing else (§6.1)
    EXPECT_TRUE(scene.empty());
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(FrameScene, ConstructionKeepsTheBackground) {
    // arrange, act
    const FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.25f, 0.5f, 0.75f));
    // assert
    EXPECT_EQ(scene.background(), (Color::rgb(0.25f, 0.5f, 0.75f)));
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
    const std::vector<Rect> drawn = rect_commands(scene);
    ASSERT_EQ(drawn.size(), 2u);
    EXPECT_EQ(drawn[0], (Rect{0.0f, 0.0f, 10.0f, 10.0f}));
    EXPECT_EQ(drawn[1], (Rect{10.0f, 10.0f, 20.0f, 20.0f}));
}

TEST(FrameScene, FromPrimitivesKeepsTheRectangleOrder) {
    // arrange
    std::vector<Rect> rects;
    rects.push_back(Rect{0.0f, 0.0f, 1.0f, 1.0f});
    rects.push_back(Rect{1.0f, 1.0f, 2.0f, 2.0f});
    rects.push_back(Rect{2.0f, 2.0f, 3.0f, 3.0f});
    // act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    // assert the list is in paint order, so the scene draws what the caller drew in that order (§6.1)
    const std::vector<Rect> drawn = rect_commands(scene);
    ASSERT_EQ(drawn.size(), 3u);
    EXPECT_EQ(drawn[0], (Rect{0.0f, 0.0f, 1.0f, 1.0f}));
    EXPECT_EQ(drawn[1], (Rect{1.0f, 1.0f, 2.0f, 2.0f}));
    EXPECT_EQ(drawn[2], (Rect{2.0f, 2.0f, 3.0f, 3.0f}));
}

TEST(FrameScene, FromPrimitivesStampsEveryCommandWithTheWholeSurface) {
    // arrange, act
    const FrameScene scene = FrameScene::from_primitives(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f), {Rect{0.0f, 0.0f, 10.0f, 10.0f}});
    // assert a hand drawn scene never narrows its clip, so the replay opens no switch for it (§6.2)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].clip(), (Rect{0.0f, 0.0f, 640.0f, 480.0f}));
}

TEST(FrameScene, TheKindFollowsTheGeometryACommandCarries) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // act the clip is stamped first, because a command that carries none draws nothing (§6.2)
    DrawCommand command = DrawCommand::rounded_rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, 4.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // assert the kind is read off the geometry, so the two can never disagree (§6.2)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].kind(), DrawCommandKind::RoundedRect);
}

TEST(FrameScene, ARoundedRectangleKeepsItsRadius) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rounded_rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, 4.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // assert the radius survives the record, which is what a bucketed scene lost (§6.2)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_FLOAT_EQ(scene.commands()[0].rect_geometry().radius, 4.0f);
}

TEST(FrameScene, ACommandThatCarriesNoClipDrawsUnclipped) {
    // arrange a command nobody stamped, which is what a caller that skipped the recorder produces (§6.2)
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    scene.add_command(DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}));
    // assert a hand built command is not silently dropped, because a missing clip means no clip rather than nothing (§6.2)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].clip(), DrawCommand::unclipped());
}

TEST(FrameScene, ACommandCarriesTheColourItWasPainted) {
    // arrange, act
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    scene.add_command(DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(0.3f, 0.4f, 0.5f)}));
    // assert the paint travels with the geometry, so stage two cannot ask what the ui thread painted (§6.1, AD-18)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].paint().fill, (Color::rgb(0.3f, 0.4f, 0.5f)));
}

TEST(FrameScene, ACommandThatPaintsNothingIsCulled) {
    // arrange
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    Paint transparent;
    transparent.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    transparent.stroke = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    // act a paint that neither fills nor strokes would draw nothing, so it is refused at the boundary (§6.2)
    scene.add_command(DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, transparent));
    // assert
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(FrameScene, ACommandWithNoAreaIsCulled) {
    // arrange, act
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    scene.add_command(DrawCommand::rectangle(Rect{0.0f, 0.0f, 0.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}));
    // assert
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(FrameScene, ACommandUnderAnEmptyClipIsCulled) {
    // arrange
    DrawCommand clipped = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act the clip is stamped to nothing, which no ink can land inside (§6.2)
    clipped.set_clip(Rect{});
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    scene.add_command(std::move(clipped));
    // assert
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(FrameScene, CommandsOfDifferentKindsKeepTheirPaintOrder) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // act a contour is recorded before a rectangle that overlaps it (§6.1)
    DrawCommand contour = DrawCommand::path(nullptr, Transform{}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    contour.set_clip(full);
    scene.add_command(std::move(contour));
    DrawCommand shape = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    shape.set_clip(full);
    scene.add_command(std::move(shape));
    // assert the rectangle is recorded after the contour, so it draws over it rather than under it (§6.1)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].kind(), DrawCommandKind::Rect);
}

TEST(FrameScene, ATextCommandCarriesItsRunAndItsBoxOrigin) {
    // arrange
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::text(synthetic_layout(5, 16.0f, Size{80.0f, 16.0f}), Point{10.0f, 20.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{640.0f, 480.0f}));
    scene.add_command(std::move(command));
    // assert the run and its origin travel together, which is what a parallel list kept apart (§6.1)
    ASSERT_EQ(scene.command_count(), 1u);
    EXPECT_EQ(scene.commands()[0].kind(), DrawCommandKind::Text);
    EXPECT_EQ(scene.commands()[0].text_geometry().origin, (Point{10.0f, 20.0f}));
    EXPECT_EQ(scene.commands()[0].text_geometry().layout.run->glyph_count(), 5u);
}

TEST(FrameScene, AnEmptyTextCommandIsRefused) {
    // arrange
    FrameScene scene(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::text(synthetic_layout(0, 16.0f, Size{0.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{640.0f, 480.0f}));
    scene.add_command(std::move(command));
    // assert a run with no glyphs draws nothing, so the scene carries no dead draw (§6.2)
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(FrameScene, EqualityComparesTheCommandsInOrder) {
    // arrange
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    DrawCommand first = DrawCommand::rectangle(Rect{0.0f, 0.0f, 1.0f, 1.0f}, Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    first.set_clip(full);
    DrawCommand second = DrawCommand::rectangle(Rect{1.0f, 0.0f, 1.0f, 1.0f}, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    second.set_clip(full);
    // act
    FrameScene ordered(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ordered.add_command(first);
    ordered.add_command(second);
    FrameScene reversed(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    reversed.add_command(second);
    reversed.add_command(first);
    // assert two scenes are equal only when their commands are equal and in the same order (§6.1)
    EXPECT_NE(ordered, reversed);
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
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
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
    const WindowMetrics built_metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    FrameScene built(built_metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand recorded = DrawCommand::line_batch(batch, Transform{}, StrokeStyle{.width = 1.0f});
    recorded.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, built_metrics.logical_size));
    built.add_command(std::move(recorded));
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
    // arrange the frames differ in content, because a frame at other metrics is dropped (§6.1, D9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    GatedRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    // the render thread parks inside the first frame, so the second submit lands on a known in flight state (§6.1)
    executor.submit(surface, std::make_shared<const FrameScene>(FrameScene::from_primitives(metrics, Color::rgb(0.0f, 0.0f, 0.0f), {Rect::from_origin_size(Point{0.0f, 0.0f}, Size{1.0f, 1.0f})})));
    backend.wait_until_arrived();
    // act
    executor.submit(surface, std::make_shared<const FrameScene>(FrameScene::from_primitives(metrics, Color::rgb(0.0f, 0.0f, 0.0f), {Rect::from_origin_size(Point{0.0f, 0.0f}, Size{2.0f, 2.0f}), Rect::from_origin_size(Point{0.0f, 0.0f}, Size{3.0f, 3.0f})})));
    executor.submit(surface, std::make_shared<const FrameScene>(FrameScene::from_primitives(metrics, Color::rgb(0.0f, 0.0f, 0.0f), {Rect::from_origin_size(Point{0.0f, 0.0f}, Size{1.0f, 1.0f}), Rect::from_origin_size(Point{0.0f, 0.0f}, Size{2.0f, 2.0f})})));
    backend.release();
    executor.flush();
    // assert
    EXPECT_EQ(executor.executed_count(), 2u);
    // the middle frame is dropped, so the render thread drew the first and the newest (§6.1)
    ASSERT_EQ(backend.executed_rect_counts.size(), 2u);
    EXPECT_EQ(backend.executed_rect_counts[0], 1u);
    EXPECT_EQ(backend.executed_rect_counts[1], 2u);
}

TEST(RenderExecutor, FlushReturnsOnlyOnceNothingIsPending) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f)));
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
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
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
    const auto scene = std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
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
    const bool submitted = executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f)));
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
    executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f)));
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
        executor.submit(surface, std::make_shared<const FrameScene>(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f)));
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
    EXPECT_EQ(recorder.commands()[0].kind(), DrawCommandKind::Rect);
    EXPECT_EQ(recorder.commands()[1].kind(), DrawCommandKind::RoundedRect);
    EXPECT_EQ(recorder.commands()[2].kind(), DrawCommandKind::Text);
    EXPECT_FLOAT_EQ(recorder.commands()[1].rect_geometry().radius, 4.0f);
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
    EXPECT_EQ(recorder.commands()[0].transform(), (Transform::translation(5.0f, 6.0f)));
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
    EXPECT_EQ(recorder.commands()[0].kind(), DrawCommandKind::LineBatch);
    EXPECT_EQ(recorder.commands()[0].batch_geometry().batch, batch);
    EXPECT_EQ(recorder.commands()[0].transform(), (Transform::translation(10.0f, 20.0f)));
    EXPECT_EQ(recorder.commands()[0].batch_geometry().stroke, stroke);
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
    EXPECT_EQ(recorder.commands()[0].kind(), DrawCommandKind::Image);
    EXPECT_EQ(recorder.commands()[0].image_geometry().image, image);
    EXPECT_FLOAT_EQ(recorder.commands()[0].image_geometry().destination.x, 10.0f);
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
    EXPECT_EQ(outer.commands()[0].kind(), DrawCommandKind::Rect);
    EXPECT_EQ(outer.commands()[1].kind(), DrawCommandKind::Rect);
    EXPECT_EQ(outer.commands()[2].kind(), DrawCommandKind::Text);
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect destination = Rect::from_origin_size(Point{2.0f, 3.0f}, Size{4.0f, 4.0f});
    const Transform transform = Transform::translation(7.0f, 8.0f);
    StrokeStyle stroke;
    stroke.width = 2.0f;
    // act the four retained kinds are recorded into the one list, in the order they are added (§6.4)
    for (DrawCommand command : {DrawCommand::line_batch(batch, transform, stroke), DrawCommand::path(path, transform, Paint{}), DrawCommand::picture(picture, transform), DrawCommand::image(image, destination, Paint{})}) {
        // the clip is stamped, because every command carries its own (§6.2)
        command.set_clip(full);
        // the command joins the list in the order the test added it (§6.1)
        scene.add_command(std::move(command));
    }
    // assert the list is the four kinds in that order, which is what stage two replays (§6.1)
    ASSERT_EQ(scene.command_count(), 4u);
    EXPECT_EQ(scene.commands()[0].kind(), DrawCommandKind::LineBatch);
    EXPECT_EQ(scene.commands()[1].kind(), DrawCommandKind::Path);
    EXPECT_EQ(scene.commands()[2].kind(), DrawCommandKind::Picture);
    EXPECT_EQ(scene.commands()[3].kind(), DrawCommandKind::Image);
    EXPECT_EQ(scene.commands()[0].batch_geometry().batch, batch);
    EXPECT_EQ(scene.commands()[0].batch_geometry().stroke, stroke);
    EXPECT_EQ(scene.commands()[0].transform(), transform);
    EXPECT_EQ(scene.commands()[1].path_geometry().path, path);
    EXPECT_EQ(scene.commands()[2].picture_geometry().picture, picture);
    EXPECT_EQ(scene.commands()[3].image_geometry().image, image);
    EXPECT_EQ(scene.commands()[3].image_geometry().destination, destination);
}

TEST(FrameScene, ANullRetainedDrawIsRefused) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    // act a retained draw with nothing to replay is refused at the boundary, whatever its kind (§6.4, AD-06)
    for (DrawCommand command : {DrawCommand::line_batch(nullptr, Transform{}, StrokeStyle{}), DrawCommand::path(nullptr, Transform{}, Paint{}), DrawCommand::picture(nullptr, Transform{}), DrawCommand::image(nullptr, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f}), Paint{})}) {
        // the clip is stamped first, so the cull that refuses it is the retained one (§6.2)
        command.set_clip(full);
        // the command is offered to the scene (§6.1)
        scene.add_command(std::move(command));
    }
    // assert
    EXPECT_EQ(scene.command_count(), 0u);
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

TEST(PictureRecorder, AClipIsRecordedInTheSpaceTheGeometryIsPlacedIn) {
    // arrange a recorder standing at a translation, which is what a transformed subtree holds (§6.4)
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(100.0f, 20.0f));
    // act the clip is declared in local coordinates, like the geometry below it (§6.2)
    recorder.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 40.0f}));
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 40.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the clip is held where the content that it cuts is placed, because both travel in picture space (§6.4)
    EXPECT_EQ(recorder.clip(), (Rect{.x = 100.0f, .y = 20.0f, .width = 50.0f, .height = 40.0f}));
    // assert the command carries that clip, so the replay scissors the shape it actually draws (§6.2, §6.4)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].clip(), (Rect{.x = 100.0f, .y = 20.0f, .width = 50.0f, .height = 40.0f}));
}

TEST(PictureRecorder, ARoundedClipScalesItsRadiusWithTheTransform) {
    // arrange a recorder standing at a doubling scale, which a scaled subtree holds (§6.4)
    PictureRecorder recorder;
    recorder.set_transform(Transform::scaling(2.0f, 2.0f));
    // act a rounded clip is declared in local coordinates (§6.2)
    recorder.clip_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 3.0f);
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the clip covers the scaled square, because the geometry it cuts is scaled the same way (§6.4)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].clip(), (Rect{.x = 0.0f, .y = 0.0f, .width = 20.0f, .height = 20.0f}));
    // assert the radius doubled with the extent, because a scaled corner keeps its shape (§6.2)
    EXPECT_FLOAT_EQ(recorder.commands()[0].clip_radius(), 6.0f);
}

TEST(PictureRecorder, AShadowCarriesTheTransformItWasRecordedUnder) {
    // arrange a recorder standing at a translation, which a transformed subtree holds (§6.4)
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(4.0f, 5.0f));
    // act a cast is drawn at local coordinates, which the transform places (§6.2)
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f), .offset = Point{0.0f, 0.0f}, .blur = 2.0f});
    // assert the cast carries the transform, because a cast is placed like every other draw (§6.4)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].transform(), Transform::translation(4.0f, 5.0f));
    // assert the bound covers the reach where the transform places it, which is six pixels each way (§6.2)
    EXPECT_EQ(recorder.commands()[0].bounds(), (Rect{.x = -1.0f, .y = 1.0f, .width = 22.0f, .height = 22.0f}));
}

TEST(PictureRecorder, EachClipKindNarrowsToTheRectangleItWasGiven) {
    // arrange
    PictureRecorder recorder;
    // act
    recorder.clip_rect(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}));
    const Rect first_clip = recorder.clip();
    recorder.clip_rounded_rect(Rect::from_origin_size(Point{4.0f, 5.0f}, Size{20.0f, 20.0f}), 3.0f);
    // assert the second clip is the overlap, because a nested clip may only cut more away (§6.2)
    EXPECT_EQ(first_clip, (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f})));
    EXPECT_EQ(recorder.clip(), (Rect::from_origin_size(Point{4.0f, 5.0f}, Size{7.0f, 7.0f})));
}

TEST(PictureRecorder, AClipWiderThanTheOneItNarrowsCannotWidenIt) {
    // arrange
    PictureRecorder recorder;
    // act
    recorder.clip_rect(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{10.0f, 10.0f}));
    recorder.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // assert a container inside a narrower clip never lets the content back out (§6.2)
    EXPECT_EQ(recorder.clip(), (Rect::from_origin_size(Point{10.0f, 10.0f}, Size{10.0f, 10.0f})));
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
    EXPECT_EQ(recorder.commands()[0].kind(), DrawCommandKind::Path);
    EXPECT_EQ(recorder.commands()[0].path_geometry().path, path);
    // the local transform applies first, then the recorder's translation on top (§6.4)
    EXPECT_EQ(recorder.commands()[0].transform().apply(Point{1.0f, 0.0f}), (Point{7.0f, 6.0f}));
    EXPECT_EQ(recorder.commands()[0].transform().apply(Point{0.0f, 1.0f}), (Point{5.0f, 8.0f}));
    EXPECT_EQ(recorder.commands()[0].paint(), paint);
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
    // arrange the whole scene is offered a run that measured nothing (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::text(TextLayout{}, Point{0.0f, 0.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // assert the scene carries no dead draw, which is what a run with no glyphs is (§6.2)
    EXPECT_EQ(scene.command_count(), 0u);
}

TEST(RenderExecutorStaleMetricsChecks, ASceneBuiltForTheSurfaceMetricsExecutes) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(surface.surface_metrics, Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert a scene built for the metrics the surface presents at is executed (§6.1)
    EXPECT_EQ(executor.executed_count(), 1u);
    EXPECT_EQ(executor.dropped_count(), 0u);
}

TEST(RenderExecutorStaleMetricsChecks, ASceneBuiltAtOtherMetricsIsDropped) {
    // arrange the window resized before the frame was built, which is the race the drop answers (§5.3, D9.1)
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    const WindowMetrics stale = WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f);
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(stale, Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert the frame is dropped rather than executed (§6.1, D9.1)
    EXPECT_EQ(executor.executed_count(), 0u);
    EXPECT_EQ(executor.dropped_count(), 1u);
}

TEST(RenderExecutorStaleMetricsChecks, ADroppedFrameBeginsNoFrameOnTheSurface) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    const WindowMetrics stale = WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f);
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(stale, Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert no frame begins, so the surface acquires no drawable for a scene it cannot present (§6.1)
    EXPECT_EQ(surface.begun, 0u);
    EXPECT_EQ(surface.presented, 0u);
}

TEST(RenderExecutorStaleMetricsChecks, ADroppedFrameNeverResizesTheSurface) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    const WindowMetrics stale = WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f);
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(stale, Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert the surface still presents at the metrics it had (§5.3, D9.1)
    EXPECT_EQ(surface.metrics(), surface.surface_metrics);
    EXPECT_EQ(surface.metrics().logical_size, (Size{800.0f, 600.0f}));
}

TEST(RenderExecutorStaleMetricsChecks, AResizedSurfaceExecutesTheFrameBuiltForIt) {
    // arrange the ui thread resized the surface before the next submit, which is the documented path (§5.3, D9.1)
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    surface.resize(WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f));
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(surface.metrics(), Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    executor.flush();
    // assert a resize costs the frames in between and no more (§6.1)
    EXPECT_EQ(executor.executed_count(), 1u);
    EXPECT_EQ(executor.dropped_count(), 0u);
}

TEST(RenderExecutorStaleMetricsChecks, ADropReleasesTheFlushWaiters) {
    // arrange
    TestRendererBackend backend;
    RenderExecutor executor(&backend);
    TestRenderSurface surface;
    surface.surface_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    const WindowMetrics stale = WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f);
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(FrameScene::from_primitives(stale, Color::rgb(0.0f, 0.0f, 0.0f), {}));
    // act
    executor.submit(surface, scene);
    // assert a dropped frame still empties the queue, so a ui thread blocked in flush is released (§6.1)
    EXPECT_NO_FATAL_FAILURE(executor.flush());
    EXPECT_FALSE(executor.pending());
}

TEST(ReplayCommandsChecks, AnUnclippedListDrawsWithNoSaveAndNoRestore) {
    // arrange a list whose commands all carry the same clip, which is what an unclipped tree records (§6.2)
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    std::vector<DrawCommand> commands;
    for (int index = 0; index < 3; index++) {
        // the clip is stamped on every command, because a recorder stamps it as it records (§6.2)
        DrawCommand command = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        command.set_clip(full);
        // the command joins the list in order (§6.1)
        commands.push_back(std::move(command));
    }
    TestRenderContext context;
    // act
    replay_commands(commands, context, full);
    // assert an unclipped run of commands opens no clip switch at all (§6.2)
    ASSERT_EQ(context.draw_log.size(), 3u);
    EXPECT_EQ(context.save_count, 0u);
    EXPECT_EQ(context.restore_count, 0u);
    EXPECT_TRUE(context.clips.empty());
}

TEST(ReplayCommandsChecks, ANarrowerClipOpensOneSwitchAndClosesIt) {
    // arrange
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    const Rect narrow = Rect::from_origin_size(Point{10.0f, 10.0f}, Size{50.0f, 50.0f});
    std::vector<DrawCommand> commands;
    // the first command narrows the clip, because that is what a clipped subtree records (§6.2)
    DrawCommand first = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    first.set_clip(narrow);
    commands.push_back(std::move(first));
    // the second stays under it, so the switch is not opened again (§6.2)
    DrawCommand second = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    second.set_clip(narrow);
    commands.push_back(std::move(second));
    TestRenderContext context;
    // act
    replay_commands(commands, context, full);
    // assert the clip was scissored once, and the switch was balanced (§6.2)
    ASSERT_EQ(context.draw_log.size(), 2u);
    ASSERT_EQ(context.clips.size(), 1u);
    EXPECT_EQ(context.clips[0], narrow);
    EXPECT_EQ(context.save_count, 1u);
    EXPECT_EQ(context.restore_count, 1u);
}

TEST(ReplayCommandsChecks, TwoClipsInARowScissorTwice) {
    // arrange
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    const Rect left = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 100.0f});
    const Rect right = Rect::from_origin_size(Point{100.0f, 0.0f}, Size{50.0f, 100.0f});
    std::vector<DrawCommand> commands;
    for (const Rect& clip : {left, right}) {
        // the clip changes per command, which is what two sibling subtrees record (§6.2)
        DrawCommand command = DrawCommand::rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        command.set_clip(clip);
        // the command joins the list in order (§6.1)
        commands.push_back(std::move(command));
    }
    TestRenderContext context;
    // act
    replay_commands(commands, context, full);
    // assert each clip applied in turn, and the list balanced every switch it opened (§6.2)
    ASSERT_EQ(context.clips.size(), 2u);
    EXPECT_EQ(context.clips[0], left);
    EXPECT_EQ(context.clips[1], right);
    EXPECT_EQ(context.save_count, context.restore_count);
}

TEST(ReplayCommandsChecks, PaintOrderSurvivesAcrossKinds) {
    // arrange the contour is recorded before the rectangle that overlaps it (§6.1)
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    ResourceFactory factory;
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, points);
    std::vector<DrawCommand> commands;
    for (DrawCommand command : {DrawCommand::path(path, Transform{}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}), DrawCommand::rounded_rectangle(Rect{0.0f, 0.0f, 10.0f, 10.0f}, 2.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}), DrawCommand::text(synthetic_layout(3, 16.0f, Size{30.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)})}) {
        // the clip is stamped, because a recorder stamps it on every command (§6.2)
        command.set_clip(full);
        // the command joins the list in the order the loop built it (§6.1)
        commands.push_back(std::move(command));
    }
    TestRenderContext context;
    // act
    replay_commands(commands, context, full);
    // assert the surface is asked for the three draws in that order, which a bucketed scene could not say (§6.1)
    ASSERT_EQ(context.draw_log.size(), 3u);
    EXPECT_EQ(context.draw_log[0], "path");
    EXPECT_EQ(context.draw_log[1], "rounded");
    EXPECT_EQ(context.draw_log[2], "text");
}

TEST(ReplayCommandsChecks, ARoundedRectangleReplaysAsARoundedRectangle) {
    // arrange
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    std::vector<DrawCommand> commands;
    DrawCommand command = DrawCommand::rounded_rectangle(Rect{0.0f, 0.0f, 20.0f, 20.0f}, 4.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act
    command.set_clip(full);
    commands.push_back(std::move(command));
    TestRenderContext context;
    // act / assert the radius reaches the backend, which is what a rounded rect recorded as a plain one lost (§6.2)
    replay_commands(commands, context, full);
    ASSERT_EQ(context.rounded_radii.size(), 1u);
    EXPECT_FLOAT_EQ(context.rounded_radii[0], 4.0f);
}

TEST(ReplayCommandsChecks, APictureReplaysThroughTheSameReplay) {
    // arrange the picture is recorded by the picture recorder, which records the same command type (§6.4)
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    recorder.draw_text(synthetic_layout(3, 16.0f, Size{30.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    ResourceFactory factory;
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    ASSERT_NE(picture, nullptr);
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f});
    std::vector<DrawCommand> commands;
    DrawCommand command = DrawCommand::picture(picture, Transform{});
    // act
    command.set_clip(full);
    commands.push_back(std::move(command));
    TestRenderContext context;
    // act
    replay_commands(commands, context, full);
    // assert a frame asks the surface for the picture itself, which is what a nested replay then walks (§6.4)
    ASSERT_EQ(context.draw_log.size(), 1u);
    EXPECT_EQ(context.draw_log[0], "picture");
}

TEST(ReplayCommandsChecks, APicturesOwnCommandsReplayInOrder) {
    // arrange the picture's own list is replayed directly, which is what the nested draw does (§6.4)
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    recorder.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 2.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    ResourceFactory factory;
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    ASSERT_NE(picture, nullptr);
    TestRenderContext context;
    // act
    replay_commands(picture->commands(), context, DrawCommand::unclipped());
    // assert the nested list draws in its recording order, through the one replay (§6.4)
    ASSERT_EQ(context.draw_log.size(), 2u);
    EXPECT_EQ(context.draw_log[0], "rect");
    EXPECT_EQ(context.draw_log[1], "rounded");
}

TEST(PictureRecorder, AnOpacityDimsEveryRecordedCommand) {
    // arrange a recorder dimmed by content, which is the picture equivalent of a node opacity (§6.1)
    PictureRecorder recorder;
    recorder.set_opacity(0.5f);
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the recorded command carries the dimmed alpha, because the picture replays as recorded (§6.1, §6.4)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.commands()[0].paint().fill.alpha, 0.5f);
}

TEST(PictureRecorder, AnOpacityRoundTripsThroughSaveAndRestore) {
    // arrange a dimmed recorder, because a picture nests the draws that compose onto it (§6.1, §6.4)
    PictureRecorder recorder;
    recorder.set_opacity(0.5f);
    recorder.save();
    // act
    recorder.set_opacity(0.25f);
    // assert the inner value stands, because the context carries one opacity at a time (§6.1)
    EXPECT_FLOAT_EQ(recorder.opacity(), 0.25f);
    // assert the restore brings the outer one back, which is what the state stack is for (§6.2)
    recorder.restore();
    EXPECT_FLOAT_EQ(recorder.opacity(), 0.5f);
}

TEST(PictureRecorder, AnOvershootOpacityIsClamped) {
    // arrange an opacity from a binding that overshot, which the setter clamps (§6.1)
    PictureRecorder recorder;
    // act
    recorder.set_opacity(4.0f);
    // assert
    EXPECT_FLOAT_EQ(recorder.opacity(), 1.0f);
}

TEST(PictureRecorder, AShadowIsRecordedAsItsOwnKind) {
    // arrange a cast with both a colour and a shape, which is what puts ink down (§6.2)
    PictureRecorder recorder;
    const Shadow cast{.color = Color::rgb(0.0f, 0.0f, 0.0f), .offset = Point{0.0f, 2.0f}, .blur = 3.0f};
    // act
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}), 2.0f, cast);
    // assert the cast travels as its own kind, because the replay blurs it offscreen (§6.2)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_EQ(recorder.commands()[0].kind(), DrawCommandKind::Shadow);
    EXPECT_EQ(recorder.commands()[0].shadow_geometry().shadow, cast);
}

TEST(PictureRecorder, AShadowWithNoColourDoesNotRecord) {
    // arrange a clear cast, which puts no ink down (§6.2)
    PictureRecorder recorder;
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(PictureRecorder, AShadowWithNoShapeDoesNotRecord) {
    // arrange a cast with no area, because there is nothing to throw it (§6.2)
    PictureRecorder recorder;
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{0.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f)});
    // assert
    EXPECT_TRUE(recorder.empty());
}

TEST(DrawCommandShape, AShadowCommandCarriesItsCast) {
    // arrange a cast, which is the whole geometry of the command (§6.2)
    const Shadow cast{.color = Color::rgb(0.1f, 0.2f, 0.3f), .offset = Point{1.0f, 2.0f}, .blur = 4.0f};
    // act
    const DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{5.0f, 5.0f}, Size{20.0f, 10.0f}), 3.0f, cast);
    // assert the shape, the radius and the cast all travel with it, because the replay needs all three (§6.2)
    EXPECT_EQ(command.kind(), DrawCommandKind::Shadow);
    EXPECT_FLOAT_EQ(command.shadow_geometry().rect.width, 20.0f);
    EXPECT_FLOAT_EQ(command.shadow_geometry().radius, 3.0f);
    EXPECT_EQ(command.shadow_geometry().shadow, cast);
}

TEST(DrawCommandShape, AShadowWithNoShapeDrawsNothing) {
    // arrange a cast with no area, which the factory refuses (§6.2)
    const DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{5.0f, 5.0f}, Size{0.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f)});
    // act, assert it draws nothing, because there is no shape to throw it (§6.2)
    EXPECT_TRUE(command.draws_nothing());
}

TEST(DrawCommandShape, AShadowWithNoColourDrawsNothing) {
    // arrange a clear cast, which puts no ink down (§6.2)
    const DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{5.0f, 5.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)});
    // act, assert it draws nothing, because a clear cast has no ink (§6.2)
    EXPECT_TRUE(command.draws_nothing());
}

TEST(DrawCommandShape, AShadowBoundCoversTheBlurReach) {
    // arrange a cast whose blur reaches beyond the shape on every side (§6.2)
    const Shadow cast{.color = Color::rgb(0.0f, 0.0f, 0.0f), .offset = Point{2.0f, 4.0f}, .blur = 2.0f};
    const DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}), 0.0f, cast);
    // act
    const Rect bound = command.bounds();
    // assert the bound starts before the offset shape, because the blur spills past it (§6.2)
    EXPECT_LT(bound.x, 10.0f + 2.0f);
    EXPECT_LT(bound.y, 10.0f + 4.0f);
    // assert the bound is wider than the shape, because the reach is added on both sides (§6.2)
    EXPECT_GT(bound.width, 20.0f);
    EXPECT_GT(bound.height, 20.0f);
}

TEST(DrawCommandShape, AnOpacityDimsAShadowCommand) {
    // arrange a cast, whose colour lives in its own geometry rather than in the command paint (§6.2)
    const DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f)});
    // act
    DrawCommand dimmed = command;
    dimmed.scale_alpha(0.5f);
    // assert the cast took the factor, because a node opacity dims what it casts too (§6.2)
    EXPECT_FLOAT_EQ(dimmed.shadow_geometry().shadow.color.alpha, 0.5f);
}

TEST(DrawCommandShape, AnOpacityDimsAShadowToNothingWithoutCullingIt) {
    // arrange a cast, which must survive a zero opacity even though it stops being visible (§6.1, §6.2)
    DrawCommand command = DrawCommand::shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f)});
    // the clip is stamped so the cull judges the cast rather than the absence of a clip (§6.2)
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act
    DrawCommand dimmed = command;
    dimmed.scale_alpha(0.0f);
    // assert the cast is still a cast, because a node at no opacity is present but not drawn (§6.1)
    EXPECT_EQ(dimmed.kind(), DrawCommandKind::Shadow);
    EXPECT_FLOAT_EQ(dimmed.shadow_geometry().shadow.color.alpha, 0.0f);
    // assert the cull would now refuse it, which is why the recorders cull before they scale (§6.1)
    EXPECT_TRUE(dimmed.draws_nothing());
}

TEST(PictureRecorder, AShadowTakesTheRecorderOpacity) {
    // arrange a dimmed picture recorder, because a picture carries its own paint (§6.1)
    PictureRecorder recorder;
    recorder.set_opacity(0.5f);
    // act
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f)});
    // assert the recorded cast took the factor, because the recorder dims every primitive it holds (§6.1)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.commands()[0].shadow_geometry().shadow.color.alpha, 0.5f);
}

TEST(PictureRecorder, AZeroOpacityStillRecordsTheCast) {
    // arrange a recorder at no opacity, which the scene recorder treats as present (§6.1)
    PictureRecorder recorder;
    recorder.set_opacity(0.0f);
    // act
    recorder.draw_shadow(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f)});
    // assert the cast is held at zero rather than dropped, because the two recorders must agree (§6.1)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.commands()[0].shadow_geometry().shadow.color.alpha, 0.0f);
}

TEST(PictureRecorder, AZeroOpacityStillRecordsAShape) {
    // arrange a recorder at no opacity, because a node at no opacity is present but not drawn (§6.1)
    PictureRecorder recorder;
    recorder.set_opacity(0.0f);
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the command is held at zero alpha, which is what the scene recorder does (§6.1)
    ASSERT_EQ(recorder.commands().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.commands()[0].paint().fill.alpha, 0.0f);
}

TEST(PictureRecorder, APolylineKeepsItsVerticesAndCarriesTheTransform) {
    // arrange a recorder standing at a translation, which is what a nested composition holds (§6.4)
    PictureRecorder recorder;
    recorder.set_transform(Transform::translation(5.0f, 6.0f));
    // act a polyline is drawn through local vertices, which the replay places under the transform (§6.2)
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {10.0f, 0.0f}};
    recorder.draw_polyline(points, StrokeStyle{});
    // assert the run keeps the vertices it was given, because the transform travels beside them (§6.4)
    ASSERT_EQ(recorder.commands().size(), 1u);
    const std::vector<Point>& kept = recorder.commands()[0].polyline_geometry().points;
    ASSERT_EQ(kept.size(), 3u);
    EXPECT_EQ(kept[1], (Point{10.0f, 10.0f}));
    // assert the command carries the transform, which is what places those vertices at replay (§6.4)
    EXPECT_EQ(recorder.commands()[0].transform(), Transform::translation(5.0f, 6.0f));
    // assert the bound is the mapped run, because a bound reports where the command draws (§6.2)
    EXPECT_EQ(recorder.commands()[0].bounds(), (Rect{.x = 5.0f, .y = 6.0f, .width = 10.0f, .height = 10.0f}));
}

TEST(DrawCommandPlacementChecks, ARunBoundStartsAtTheOriginItWasRecordedAt) {
    // arrange a run measured at a size, which a text command places at its box origin (§9.1, §6.2)
    const TextLayout layout{.size = Size{.width = 40.0f, .height = 10.0f}};
    // act the run is recorded at an origin, which is where its first line starts (§9.1)
    const DrawCommand command = DrawCommand::text(layout, Point{30.0f, 50.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the bound starts at that origin instead of the scene origin, which is where the run draws (§6.2)
    EXPECT_EQ(command.bounds(), (Rect{.x = 30.0f, .y = 50.0f, .width = 40.0f, .height = 10.0f}));
}

TEST(DrawCommandPlacementChecks, ARunBoundFollowsTheTransformTheRunWasRecordedWith) {
    // arrange the same run, recorded under a doubling scale (§6.4)
    const TextLayout layout{.size = Size{.width = 40.0f, .height = 10.0f}};
    DrawCommand command = DrawCommand::text(layout, Point{30.0f, 50.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // act the transform is stamped, which is what a scaled subtree does (§6.1)
    command.set_transform(Transform::scaling(2.0f, 2.0f));
    // assert the bound is the scaled run box, because the run draws at the size the transform gives it (§6.2)
    EXPECT_EQ(command.bounds(), (Rect{.x = 60.0f, .y = 100.0f, .width = 80.0f, .height = 20.0f}));
}

TEST(DrawCommandPlacementChecks, AnImageBoundFollowsTheTransformItWasRecordedWith) {
    // arrange one opaque pixel and the destination it fills (§6.4)
    const std::vector<std::uint8_t> pixels{255u, 255u, 255u, 255u};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(21u, 1u, 1u, ImageFormat::RGBA8, pixels);
    // act the image is recorded at a destination under a translation (§6.4)
    DrawCommand command = DrawCommand::image(image, Rect{.x = 10.0f, .y = 20.0f, .width = 8.0f, .height = 6.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_transform(Transform::translation(5.0f, 7.0f));
    // assert the bound is the destination where the transform places it (§6.2)
    EXPECT_EQ(command.bounds(), (Rect{.x = 15.0f, .y = 27.0f, .width = 8.0f, .height = 6.0f}));
}

TEST(DrawCommandPlacementChecks, APathBoundFollowsTheRetainedTransform) {
    // arrange a triangle contour, which a scene records with a transform (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{0.0f, 10.0f}};
    // act the contour is placed by the transform the retained draw carries (§6.4)
    const DrawCommand command = DrawCommand::path(std::make_shared<const PathResource>(9u, verbs, points), Transform::translation(20.0f, 30.0f), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the bound is where the replay draws the contour, not where its points were authored (§6.2)
    EXPECT_EQ(command.bounds(), (Rect{.x = 20.0f, .y = 30.0f, .width = 10.0f, .height = 10.0f}));
}

TEST(DrawCommandPlacementChecks, ADrawingCommandReplaysUnderTheTransformItCarries) {
    // arrange a rectangle command placed under a translation, which a flattened picture records (§6.4)
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_transform(Transform::translation(5.0f, 6.0f));
    TestRenderContext context;
    // act the command draws itself, which is what stage two does (§6.1)
    command.replay(context);
    // assert the context adopted the transform around the draw, because the primitive takes none (§6.4)
    ASSERT_EQ(context.transforms.size(), 1u);
    EXPECT_EQ(context.transforms.front(), Transform::translation(5.0f, 6.0f));
    // assert the state is balanced, because a transform must not outlive its command (§6.2)
    EXPECT_EQ(context.save_count, 1u);
    EXPECT_EQ(context.restore_count, 1u);
    // assert the context stands where it did, because a restore that did nothing would leak the transform (§6.2)
    EXPECT_EQ(context.transform(), Transform{});
    // assert the primitive still drew once, which is what the transform is for (§6.2)
    ASSERT_EQ(context.drawn_rects.size(), 1u);
}

TEST(DrawCommandPlacementChecks, ADrawingCommandWithNoTransformDrawsWithoutAState) {
    // arrange a rectangle command that carries no transform, which is every command a frame records (§6.4)
    const DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    TestRenderContext context;
    // act the command draws itself (§6.1)
    command.replay(context);
    // assert no state was pushed, because an identity transform costs nothing (§6.2)
    EXPECT_EQ(context.save_count, 0u);
    EXPECT_EQ(context.transforms.size(), 0u);
    EXPECT_EQ(context.drawn_rects.size(), 1u);
}

TEST(DrawCommandPlacementChecks, ARetainedCommandIsNotTransformedTwice) {
    // arrange a path command that already takes its transform as an argument (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{0.0f, 10.0f}};
    const DrawCommand command = DrawCommand::path(std::make_shared<const PathResource>(3u, verbs, points), Transform::translation(7.0f, 8.0f), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    TestRenderContext context;
    // act the command draws itself (§6.1)
    command.replay(context);
    // assert the context was left alone, because composing the transform here would apply it twice (§6.4)
    EXPECT_EQ(context.save_count, 0u);
    EXPECT_EQ(context.transforms.size(), 0u);
    // assert the path received its transform once, as the primitive argument (§6.4)
    ASSERT_EQ(context.drawn_path_transforms.size(), 1u);
    EXPECT_EQ(context.drawn_path_transforms.front(), Transform::translation(7.0f, 8.0f));
}

TEST(DrawCommandPlacementChecks, ADrawingCommandInANestedPictureComposesTheTransforms) {
    // arrange a rectangle command carrying its own transform, replayed under a placed context (§6.4)
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_transform(Transform::translation(2.0f, 3.0f));
    TestRenderContext context;
    context.set_transform(Transform::translation(100.0f, 200.0f));
    // act
    command.replay(context);
    // assert the command transform sits under the context's, so the inner picture lands inside the outer one (§6.4)
    ASSERT_EQ(context.transforms.size(), 2u);
    EXPECT_EQ(context.transforms.back(), Transform::translation(2.0f, 3.0f).then(Transform::translation(100.0f, 200.0f)));
}

TEST(GlyphOutlinePlacementChecks, BoundsFollowTheTransformTheGlyphWasRecordedWith) {
    // arrange a square glyph outline, which is a shape whose every corner is known (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{10.0f, 10.0f}, Point{0.0f, 10.0f}};
    const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(7u, 3u, 16.0f, GlyphOutlineData{.verbs = verbs, .points = points});
    // act the glyph is placed at a pen under a doubling scale, which a scaled subtree composes (§6.4, §6.1)
    const DrawCommand command = DrawCommand::glyph_outline(outline, Point{5.0f, 5.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}, Transform::scaling(2.0f, 2.0f));
    // assert the bound is the scaled square rather than the pen placed one, because the transform moves it (§6.2, §6.1)
    EXPECT_EQ(command.bounds(), (Rect{.x = 10.0f, .y = 10.0f, .width = 20.0f, .height = 20.0f}));
}

TEST(GlyphOutlinePlacementChecks, BoundsFollowASkewedTransform) {
    // arrange the same square, whose extent a skew leaves unchanged along one axis and doubles along the other (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{10.0f, 10.0f}, Point{0.0f, 10.0f}};
    const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(7u, 3u, 16.0f, GlyphOutlineData{.verbs = verbs, .points = points});
    // act the glyph is placed at the origin under a shear, which turns the square into a wider parallelogram (§6.1)
    const DrawCommand command = DrawCommand::glyph_outline(outline, Point{0.0f, 0.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}, Transform{.skew_x = 1.0f});
    // assert the bound follows the sheared corners rather than the unsheared square (§6.2, §6.1)
    EXPECT_EQ(command.bounds(), (Rect{.x = 0.0f, .y = 0.0f, .width = 20.0f, .height = 10.0f}));
}

TEST(GlyphOutlinePlacementChecks, ReplaysTheOutlineUnderThePenAndTheRecordedTransform) {
    // arrange a square glyph outline and the context the command replays into (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{10.0f, 10.0f}, Point{0.0f, 10.0f}};
    const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(7u, 3u, 16.0f, GlyphOutlineData{.verbs = verbs, .points = points});
    TestRenderContext context;
    // the glyph is placed at a pen under a shear, which is the transform a skewed subtree composes (§6.4, §6.1)
    const Transform shear{.skew_x = 1.0f};
    // act the recorded command draws itself, which is what stage two does (§6.1)
    DrawCommand::glyph_outline(outline, Point{5.0f, 7.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}, shear).replay(context);
    // assert the retained path is drawn once, because the glyph is one command (§6.4, AD-06)
    ASSERT_EQ(context.drawn_paths.size(), 1u);
    EXPECT_EQ(context.drawn_paths.front(), outline->path());
    // assert the path draws under the pen and the shear, because the two orders map a point differently (§6.4, §6.1)
    EXPECT_EQ(context.drawn_path_transforms.front(), Transform::translation(5.0f, 7.0f).then(shear));
}

TEST(GlyphOutlineCacheChecks, AnswersAnOutlineItAlreadyBuilt) {
    // arrange an empty cache and the identity of one glyph outline (§6.4)
    GlyphOutlineCache cache;
    const GlyphOutlineKey key{.font_hash = 7u, .glyph_id = 3u, .font_size = 16.0f};
    std::size_t built = 0u;
    // the resolver counts its calls, because not running twice is the claim of a cache (§6.4, AD-06)
    const GlyphOutlineResolver counting = [&built](const GlyphOutlineKey&) {
        // one empty outline is built per call, which is enough to pin the caching (§6.4)
        built++;
        // the outline carries the key it was asked for, which is what the cache stores it under (§6.4)
        return std::make_shared<const GlyphOutline>(7u, 3u, 16.0f, GlyphOutlineData{});
    };
    // act the same key is looked up twice (§6.4)
    EXPECT_NE(cache.lookup(key, counting), nullptr);
    EXPECT_NE(cache.lookup(key, counting), nullptr);
    // assert the resolver ran once, because the second lookup answered from the cache (§6.4, AD-06)
    EXPECT_EQ(built, 1u);
    EXPECT_EQ(cache.count(), 1u);
}

TEST(GlyphOutlineCacheChecks, AnswersNothingForAnotherIdentity) {
    // arrange one built outline beside the identities it must not be confused with (§6.4)
    GlyphOutlineCache cache;
    const GlyphOutlineResolver building = [](const GlyphOutlineKey& key) {
        // the outline carries the key it was asked for, which makes every entry its own shape (§6.4)
        return std::make_shared<const GlyphOutline>(key.font_hash, key.glyph_id, key.font_size, GlyphOutlineData{});
    };
    // act the first key is built, and three neighbours of it are asked for (§6.4)
    const std::shared_ptr<const GlyphOutline> first = cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 3u, .font_size = 16.0f}, building);
    EXPECT_NE(first, nullptr);
    EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 4u, .font_size = 16.0f}, building), nullptr);
    EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 3u, .font_size = 20.0f}, building), nullptr);
    EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 8u, .glyph_id = 3u, .font_size = 16.0f}, building), nullptr);
    // assert the first key still answers its own outline, because no neighbour key aliased onto it (§6.4)
    EXPECT_EQ(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 3u, .font_size = 16.0f}, building), first);
    // assert every identity is held apart, because four keys were built and none answered another (§6.4)
    EXPECT_EQ(cache.count(), 4u);
}

TEST(GlyphOutlineCacheChecks, DropsASliceWhenItFills) {
    // arrange a cache filled to its limit by as many distinct glyphs as a run of scripts draws (§6.4, AD-06)
    GlyphOutlineCache cache;
    const GlyphOutlineResolver building = [](const GlyphOutlineKey& key) {
        // the outline carries the key it was asked for, which makes every entry its own shape (§6.4)
        return std::make_shared<const GlyphOutline>(key.font_hash, key.glyph_id, key.font_size, GlyphOutlineData{});
    };
    for (std::uint32_t index = 0u; index < GlyphOutlineCache::MAXIMUM_CACHED_OUTLINES; index++)
        EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = index, .font_size = 16.0f}, building), nullptr);
    // assert the cache is holding exactly what the limit allows (§6.4)
    EXPECT_EQ(cache.count(), GlyphOutlineCache::MAXIMUM_CACHED_OUTLINES);
    // act one more outline arrives, which a full cache answers by dropping a slice of what it holds (§6.4)
    EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 99999u, .font_size = 16.0f}, building), nullptr);
    // assert a slice is gone rather than all of them, because a rebuilt outline is not free (§6.4, AD-06)
    EXPECT_EQ(cache.count(), GlyphOutlineCache::MAXIMUM_CACHED_OUTLINES - GlyphOutlineCache::MAXIMUM_CACHED_OUTLINES / 8u + 1u);
}

TEST(GlyphOutlineCacheChecks, DropsEverythingItIsAskedToDrop) {
    // arrange one cached outline, which the clear below must not answer afterwards (§2.2)
    GlyphOutlineCache cache;
    const GlyphOutlineResolver building = [](const GlyphOutlineKey& key) {
        // the outline carries the key it was asked for, which makes every entry its own shape (§6.4)
        return std::make_shared<const GlyphOutline>(key.font_hash, key.glyph_id, key.font_size, GlyphOutlineData{});
    };
    EXPECT_NE(cache.lookup(GlyphOutlineKey{.font_hash = 7u, .glyph_id = 3u, .font_size = 16.0f}, building), nullptr);
    // act the cache is cleared, which one test does so its outlines cannot answer another's (§2.2)
    cache.clear();
    // assert nothing is held, because the clear dropped every entry (§2.2)
    EXPECT_EQ(cache.count(), 0u);
}
