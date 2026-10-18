#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#import <QuartzCore/CAMetalLayer.h>

#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/render-albor/albor_renderer_backend.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"
#include "window/window_metrics.h"

using namespace albor;

// quartzcore brings the carbon rect and point into scope, so the albor shapes are named through aliases
using AlborRect = albor::Rect;
using AlborPoint = albor::Point;
using AlborSize = albor::Size;

TEST(AlborRendererBackendChecks, BringsUpTheMetalDevice) {
    // arrange
    albor::render::AlborRendererBackend backend;
    // act, assert
    EXPECT_TRUE(backend.ready());
}

TEST(AlborRendererBackendChecks, RefusesASurfaceWithoutAValidExtent) {
    // arrange
    albor::render::AlborRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, WindowMetrics{});
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(AlborRendererBackendChecks, BuildsAnOffscreenSurfaceWithoutAWindow) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 24.0f}, 2.0f);
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    // assert
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), metrics);
}

TEST(AlborRendererBackendChecks, RefusesAStaleSceneRatherThanRepairingIt) {
    // arrange an offscreen surface at one extent and a scene recorded at another
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 24.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const FrameScene stale(WindowMetrics::from_logical(AlborSize{64.0f, 48.0f}, 1.0f), Color::rgb(1.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(*surface, stale);
    // assert
    EXPECT_TRUE(surface->read_pixels().empty());
}

TEST(AlborRendererBackendChecks, RasterizesABackgroundIntoTheOffscreenTarget) {
    // arrange an offscreen surface whose scene paints a red background
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    EXPECT_EQ(pixels[0], 255u);
    EXPECT_EQ(pixels[1], 0u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 255u);
    // the last pixel of the last row carries the same colour, so the whole target was covered
    const std::size_t last = pixels.size() - 4;
    EXPECT_EQ(pixels[last], 255u);
    EXPECT_EQ(pixels[last + 3], 255u);
}

TEST(AlborRendererBackendChecks, RasterizesAnOpaqueRectangleOverTheBackground) {
    // arrange a scene that paints a black background and one white rectangle
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    // the corner stays the background, which the rectangle never reached
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[3], 255u);
    // the rectangle centre carries the green fill at full coverage
    const std::size_t centre = (8u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[centre], 0u);
    EXPECT_EQ(pixels[centre + 1], 255u);
    EXPECT_EQ(pixels[centre + 2], 0u);
    EXPECT_EQ(pixels[centre + 3], 255u);
}

TEST(AlborRendererBackendChecks, AntialiasesAFractionalEdge) {
    // arrange a rectangle whose left edge sits half a pixel inside the surface
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.5f, 0.0f}, AlborSize{14.5f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 4u * 4u);
    // the first column is half covered by the feathered edge, which halves the red over black
    const std::size_t first = 0;
    EXPECT_GT(pixels[first], 118u);
    EXPECT_LT(pixels[first], 138u);
    // the second column is fully covered, because the fringe ended inside it
    const std::size_t second = 4u;
    EXPECT_EQ(pixels[second], 255u);
    // the far column the rectangle never reaches stays the background
    const std::size_t last = (3u * 16u + 15u) * 4u;
    EXPECT_EQ(pixels[last], 0u);
}

TEST(AlborRendererBackendChecks, CutsADrawAtTheClipItCarries) {
    // arrange a scene whose one rectangle is clipped to the left half of the surface
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{16.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{8.0f, 8.0f}));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 8u * 4u);
    // inside the clip the fill lands, which the red channel proves
    const std::size_t inside = (4u * 16u + 3u) * 4u;
    EXPECT_EQ(pixels[inside], 255u);
    EXPECT_EQ(pixels[inside + 3], 255u);
    // outside the clip the background stands, because the scissor mask cut the draw away
    const std::size_t outside = (4u * 16u + 13u) * 4u;
    EXPECT_EQ(pixels[outside], 0u);
    EXPECT_EQ(pixels[outside + 3], 255u);
}

TEST(AlborRendererBackendChecks, DimsADrawByItsOpacity) {
    // arrange a rectangle recorded at half opacity over a black background
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{8.0f, 8.0f}));
    command.scale_alpha(0.5f);
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    // the half opacity premultiplies the white to about half, which the blend lays over black
    const std::size_t centre = (4u * 8u + 4u) * 4u;
    EXPECT_GT(pixels[centre], 120u);
    EXPECT_LT(pixels[centre], 136u);
    EXPECT_EQ(pixels[centre + 3], 255u);
}

TEST(AlborRendererBackendChecks, PresentsThroughAMetalLayer) {
    // arrange a layer the way a composition root hands one to the backend
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);
    CAMetalLayer* layer = [CAMetalLayer layer];
    const NativeWindowHandle handle = NativeWindowHandle::from_object((__bridge const void*)layer);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(handle, metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    const FrameScene scene(metrics, Color::rgb(0.0f, 1.0f, 0.0f));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[1], 255u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 255u);
}

TEST(AlborRendererBackendChecks, ResizesTheOffscreenTarget) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics small = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const WindowMetrics large = WindowMetrics::from_logical(AlborSize{8.0f, 6.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, small);
    ASSERT_NE(surface, nullptr);
    // act
    backend.resize_surface(*surface, large);
    const FrameScene scene(large, Color::rgb(0.0f, 0.0f, 1.0f));
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    EXPECT_EQ(surface->metrics(), large);
    ASSERT_EQ(pixels.size(), 8u * 6u * 4u);
    EXPECT_EQ(pixels[2], 255u);
}

TEST(AlborRendererBackendChecks, RefusesAResizeToAnEmptyExtent) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    backend.resize_surface(*surface, WindowMetrics{});
    // assert
    EXPECT_EQ(surface->metrics(), metrics);
}

namespace
{
    // the smallest context a surface can hand out, which no suite draws through here
    class InertRenderContext final : public RenderContext
    {
    public:
        void save() override {}
        void restore() override {}
        void set_transform(const albor::Transform& transform) override { m_transform = transform; }
        [[nodiscard]] const albor::Transform& transform() const override { return m_transform; }
        void clip_rect(const AlborRect& rect) override { m_clip = rect; }
        void clip_rounded_rect(const AlborRect& rect, float) override { m_clip = rect; }
        [[nodiscard]] const AlborRect& clip() const override { return m_clip; }
        void set_opacity(float opacity) override { m_opacity = opacity; }
        [[nodiscard]] float opacity() const override { return m_opacity; }
        void draw_rect(const AlborRect&, const albor::Paint&) override {}
        void draw_rounded_rect(const AlborRect&, float, const albor::Paint&) override {}
        void draw_polyline(const std::span<const AlborPoint>&, const albor::StrokeStyle&) override {}
        void draw_shadow(const AlborRect&, float, const albor::Shadow&) override {}
        void draw_text(const albor::TextLayout&, AlborPoint, const albor::Paint&) override {}
        void draw_line_batch(const std::shared_ptr<const LineBatch>&, const albor::Transform&, const albor::StrokeStyle&) override {}
        void draw_path(const std::shared_ptr<const PathResource>&, const albor::Transform&, const albor::Paint&) override {}
        void draw_picture(const std::shared_ptr<const PictureResource>&, const albor::Transform&) override {}
        void draw_image(const std::shared_ptr<const ImageResource>&, const AlborRect&, const albor::Paint&) override {}

    private:
        // the transform the context last adopted
        Transform m_transform;

        // the clip the context last adopted
        AlborRect m_clip;

        // the opacity the context last adopted
        float m_opacity = 1.0f;
    };

    // the smallest surface a backend can be handed, which reports itself unusable
    class UnusableRenderSurface final : public RenderSurface
    {
    public:
        // how many frames the backend began, which stays zero because it refuses stage two
        std::size_t begun = 0;

        [[nodiscard]] bool valid() const override { return false; }

        [[nodiscard]] const WindowMetrics& metrics() const override { return m_metrics; }

        void resize(const WindowMetrics& metrics) override { m_metrics = metrics; }

        RenderContext& begin_frame() override {
            // count the frame, which proves the backend never replays onto an unusable surface
            begun++;
            return m_context;
        }

        void end_frame() override {}

    private:
        // the metrics the surface reports
        WindowMetrics m_metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);

        // the context the surface hands out, which the backend never draws through here
        InertRenderContext m_context;
    };
} // namespace

TEST(AlborRendererBackendChecks, KeepsTheTargetWhenResizedToAnEmptyExtent) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->resize(WindowMetrics{});
    // assert
    EXPECT_EQ(surface->metrics(), metrics);
}

TEST(AlborRendererBackendChecks, PresentsAnEmptyFrameWithoutDrawing) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->begin_frame();
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[3], 0u);
}

TEST(AlborRendererBackendChecks, IgnoresAnEndWithoutABegin) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->end_frame();
    // assert
    EXPECT_TRUE(surface->read_pixels().empty());
}

TEST(AlborRendererBackendChecks, DrawsTwoRangesOverOneVertexBuffer) {
    // arrange two overlapping rectangles, whose index ranges and uniforms differ per draw
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand first = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    first.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(first));
    DrawCommand second = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    second.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(second));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    // the corner carries only the first rectangle, which the second never reached
    EXPECT_EQ(pixels[0], 255u);
    EXPECT_EQ(pixels[2], 0u);
    // the centre carries the second, which paints over the first in order
    const std::size_t centre = (8u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[centre], 0u);
    EXPECT_EQ(pixels[centre + 2], 255u);
}

TEST(AlborRendererBackendChecks, DefersAResizeThatArrivesMidFrame) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics small = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const WindowMetrics large = WindowMetrics::from_logical(AlborSize{8.0f, 6.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, small);
    ASSERT_NE(surface, nullptr);
    // act
    RenderContext& context = surface->begin_frame();
    context.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, small.logical_size), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    surface->resize(large);
    const WindowMetrics during = surface->metrics();
    surface->end_frame();
    // assert
    EXPECT_EQ(during, small);
    EXPECT_EQ(surface->metrics(), large);
}
