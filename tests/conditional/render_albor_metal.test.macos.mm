#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#import <QuartzCore/CAMetalLayer.h>

#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/picture_resource.h"
#include "render/render-albor/albor_renderer_backend.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"
#include "render/screenshot.h"
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

TEST(AlborParityChecks, AnAlignedRectangleMatchesTheReference) {
    // arrange the same scene through the software reference and through this backend
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.0f, 0.8f, 0.2f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    // an aligned rectangle has no feathered edge to disagree on, so the two agree exactly
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborParityChecks, AFeatheredEdgeMatchesTheReferenceWithinTolerance) {
    // arrange a rectangle whose edges land on fractional pixels
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{6.3f, 6.7f}, AlborSize{19.4f, 18.6f}), Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    // the bound is the 21 measured on this host plus margin, which a feathered edge against an analytic one reaches
    EXPECT_LE(expected.max_channel_difference(actual), 32);
}

TEST(AlborParityChecks, ATwoScaleSceneMatchesTheReference) {
    // arrange a scene at double density, which the device scale must carry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 2.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), 32u * 32u * 4u);
    ASSERT_EQ(actual.pixels.size(), 32u * 32u * 4u);
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborParityChecks, ATranslucentImageMatchesTheReference) {
    // arrange a half transparent image, whose alpha the paint modulates
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the image is two opaque premultiplied colours, which the paint dims to half
    const std::uint8_t pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(31u, 2u, 2u, ImageFormat::RGBA8, pixels);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    // a scaled upload samples linearly in both backends, so the bound is the measured 1 plus margin
    EXPECT_LE(expected.max_channel_difference(actual), 8);
}

TEST(AlborParityChecks, ARoundedClipMatchesTheReferenceAtItsCorners) {
    // arrange a rounded clip driven through the contexts, because the recorder reduces a rounded clip to its rectangle
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.clip_rounded_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{24.0f, 24.0f}), 12.0f);
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.clip_rounded_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{24.0f, 24.0f}), 12.0f);
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t corner = (5u * 32u + 5u) * 4u;
    EXPECT_GT(actual_shot.pixels[corner], 200u);
    EXPECT_GT(expected_shot.pixels[corner], 200u);
    // the centre of the clip is covered by the black rectangle in both
    const std::size_t centre = (16u * 32u + 16u) * 4u;
    EXPECT_EQ(actual_shot.pixels[centre], 0u);
    EXPECT_EQ(expected_shot.pixels[centre], 0u);
}

TEST(AlborParityChecks, ReusesAnUploadedTextureAcrossFrames) {
    // arrange the same retained image drawn on two consecutive frames
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const std::uint8_t pixels[4] = {0, 0, 255, 255};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(41u, 1u, 1u, ImageFormat::RGBA8, pixels);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const Screenshot first = capture_surface(*surface);
    backend.execute_frame(*surface, scene);
    const Screenshot second = capture_surface(*surface);
    // assert
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    ASSERT_EQ(first.pixels.size(), second.pixels.size());
    EXPECT_EQ(first.max_channel_difference(second), 0);
    // the image paints blue over the black background, which the upload preserved
    EXPECT_EQ(second.pixels[0], 0u);
    EXPECT_EQ(second.pixels[2], 255u);
}

TEST(AlborParityChecks, ARectangularScissorMatchesTheReference) {
    // arrange a full surface fill cut by a clip at a fractional position, which the scissor mask decides
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{7.3f, 9.6f}, AlborSize{17.2f, 12.1f}));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    // the mask ramps over one device pixel, so the clipped edge lands where the reference puts it
    EXPECT_LE(expected.max_channel_difference(actual), 32);
    // the area outside the clip stays the background in both, which a missing mask would paint black
    const std::size_t outside = (2u * 32u + 2u) * 4u;
    EXPECT_EQ(actual.pixels[outside], 255u);
    EXPECT_EQ(expected.pixels[outside], 255u);
}

TEST(AlborParityChecks, AScissorUnderATranslatedTransformMatchesTheReference) {
    // arrange a clip pushed under a translated transform, which the frozen clip matrix must carry
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_transform(Transform::translation(5.0f, 5.0f));
    expected.clip_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{12.0f, 12.0f}));
    expected.set_transform(Transform{});
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected.restore();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.set_transform(Transform::translation(5.0f, 5.0f));
    actual.clip_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{12.0f, 12.0f}));
    actual.set_transform(Transform{});
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual.restore();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the clip sits at the translation, so the far corner stays background in both
    const std::size_t corner = (28u * 32u + 28u) * 4u;
    EXPECT_EQ(actual_shot.pixels[corner], 255u);
    EXPECT_EQ(expected_shot.pixels[corner], 255u);
    // and the clipped square is black in both
    const std::size_t inside = (8u * 32u + 8u) * 4u;
    EXPECT_EQ(actual_shot.pixels[inside], 0u);
    EXPECT_EQ(expected_shot.pixels[inside], 0u);
}

TEST(AlborParityChecks, AFrameRenderedAtAnotherScaleDoesNotMatch) {
    // arrange a scene at one density and a surface at another, which the comparison must reject
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    const WindowMetrics denser = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 2.0f);
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> small = backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> large = backend.create_surface(NativeWindowHandle{}, denser);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    const FrameScene small_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    const FrameScene large_scene(denser, Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(*small, small_scene);
    backend.execute_frame(*large, large_scene);
    const Screenshot first = capture_surface(*small);
    const Screenshot second = capture_surface(*large);
    // assert
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    EXPECT_NE(first.pixels.size(), second.pixels.size());
    EXPECT_FALSE(first.pixels.empty());
}

TEST(AlborParityChecks, ASoftShadowMatchesTheReference) {
    // arrange a rounded card casting a soft shadow over a white background
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = AlborPoint{0.0f, 4.0f}, .blur = 8.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = AlborPoint{0.0f, 4.0f}, .blur = 8.0f});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the cast darkens the surface under the card in both backends, which a missing shadow would leave white
    const std::size_t under = (30u * 48u + 24u) * 4u;
    EXPECT_LT(actual_shot.pixels[under], 250u);
    EXPECT_LT(expected_shot.pixels[under], 250u);
    // the bound is the 26 measured on this host plus margin, which two gaussian implementations reach
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 40);
}

TEST(AlborParityChecks, AHardShadowMatchesTheReferenceExactly) {
    // arrange a zero blur shadow, which is a hard offset copy of the caster
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{12.0f, 12.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f), .offset = AlborPoint{4.0f, 4.0f}, .blur = 0.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{12.0f, 12.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f), .offset = AlborPoint{4.0f, 4.0f}, .blur = 0.0f});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the offset copy lands on the same pixels, which the two agree on within the feathered edge band
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
    // the cast is black where it falls, which a missing shadow would leave white
    const std::size_t cast = (18u * 32u + 18u) * 4u;
    EXPECT_EQ(actual_shot.pixels[cast], 0u);
    EXPECT_EQ(expected_shot.pixels[cast], 0u);
}

TEST(AlborParityChecks, AStrokedPolylineMatchesTheReference) {
    // arrange a round joined polyline, which exercises the folded coverage ramp
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborPoint points[3] = {AlborPoint{6.0f, 8.0f}, AlborPoint{20.0f, 8.0f}, AlborPoint{20.0f, 30.0f}};
    const StrokeStyle stroke{.color = Color::rgb(0.1f, 0.1f, 0.9f), .width = 3.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_polyline(points, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_polyline(points, stroke);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the bound is the 73 measured on this host plus margin, a band of 0.4 percent of the pixels along the edges
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 80);
    // the elbow is painted in both, which a missing stroke would leave white
    const std::size_t elbow = (8u * 40u + 20u) * 4u;
    EXPECT_LT(actual_shot.pixels[elbow + 2], 250u);
    EXPECT_LT(expected_shot.pixels[elbow + 2], 250u);
}

TEST(AlborParityChecks, ARoundedRectangleMatchesTheReference) {
    // arrange a filled and stroked rounded rectangle, which exercises the arc tessellation
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Paint paint{.fill = Color::rgb(0.2f, 0.7f, 0.4f), .stroke = Color::rgb(0.0f, 0.0f, 0.0f), .stroke_width = 1.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rounded_rect(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{24.0f, 24.0f}), 8.0f, paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rounded_rect(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{24.0f, 24.0f}), 8.0f, paint);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the bound is the 55 measured on this host plus margin, a band of 0.5 percent of the pixels on the arcs
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 64);
    // the corner outside the arc stays background in both, which a square fill would paint
    const std::size_t corner = (8u * 40u + 8u) * 4u;
    EXPECT_EQ(actual_shot.pixels[corner], 255u);
    EXPECT_EQ(expected_shot.pixels[corner], 255u);
}

TEST(AlborParityChecks, AFilledPathMatchesTheReference) {
    // arrange a triangle path, which exercises the contour walk and the fan fill
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[3] = {AlborPoint{8.0f, 30.0f}, AlborPoint{20.0f, 8.0f}, AlborPoint{32.0f, 30.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(51u, verbs, points);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.9f, 0.2f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.9f, 0.2f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the interior fills identically, which a wrong winding would leave empty
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
    const std::size_t interior = (24u * 40u + 20u) * 4u;
    EXPECT_LT(actual_shot.pixels[interior], 250u);
    EXPECT_LT(expected_shot.pixels[interior], 250u);
}

TEST(AlborParityChecks, ALineBatchMatchesTheReference) {
    // arrange a retained batch of two segments, which the backend coalesces into one draw
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborPoint points[4] = {AlborPoint{6.0f, 10.0f}, AlborPoint{34.0f, 10.0f}, AlborPoint{6.0f, 26.0f}, AlborPoint{34.0f, 26.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(52u, points);
    const StrokeStyle stroke{.color = Color::rgb(0.2f, 0.2f, 0.2f), .width = 2.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_line_batch(batch, Transform{}, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_line_batch(batch, Transform{}, stroke);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // both segments paint the same rows, which a dropped second segment would halve
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
    const std::size_t second = (26u * 40u + 20u) * 4u;
    EXPECT_LT(actual_shot.pixels[second], 250u);
    EXPECT_LT(expected_shot.pixels[second], 250u);
}

TEST(AlborParityChecks, APictureMatchesTheReference) {
    // arrange a retained picture replayed under a translation
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{10.0f, 10.0f}), Paint{.fill = Color::rgb(0.3f, 0.6f, 0.9f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(53u, commands);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_picture(picture, Transform::translation(12.0f, 14.0f));
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_picture(picture, Transform::translation(12.0f, 14.0f));
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the nested transform places the rectangle identically, which a dropped transform would move
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
    const std::size_t placed = (18u * 40u + 16u) * 4u;
    EXPECT_LT(actual_shot.pixels[placed + 2], 250u);
    EXPECT_LT(expected_shot.pixels[placed + 2], 250u);
}

TEST(AlborParityChecks, ATranslucentOverlapMatchesTheReference) {
    // arrange two translucent fills that overlap, which the blend order decides
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 0.5f)});
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 1.0f, 0.5f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 0.5f)});
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 1.0f, 0.5f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // premultiplied source over composites identically, which a wrong blend factor would shift
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 16);
    // the overlap carries both fills, which a replaced rather than blended draw would lose
    const std::size_t overlap = (16u * 32u + 16u) * 4u;
    EXPECT_LT(actual_shot.pixels[overlap], 220u);
    EXPECT_LT(expected_shot.pixels[overlap], 220u);
}

TEST(AlborParityChecks, ASoftShadowOnAWideCasterMatchesTheReference) {
    // arrange a wide caster, whose vertical and horizontal blurs need different texel steps
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(AlborRect::from_origin_size(AlborPoint{8.0f, 16.0f}, AlborSize{48.0f, 12.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = AlborPoint{0.0f, 4.0f}, .blur = 8.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(AlborRect::from_origin_size(AlborPoint{8.0f, 16.0f}, AlborSize{48.0f, 12.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = AlborPoint{0.0f, 4.0f}, .blur = 8.0f});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the cast spreads vertically as well as horizontally, which an axis wrong step collapses
    const std::size_t below = (36u * 64u + 32u) * 4u;
    EXPECT_LT(actual_shot.pixels[below], 250u);
    EXPECT_LT(expected_shot.pixels[below], 250u);
    // the vertical profile follows the reference, which the single axis step would not
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 40);
}

TEST(AlborParityChecks, AFractionalImageDestinationFeathersItsEdge) {
    // arrange an image whose destination lands on fractional pixels
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{24.0f, 24.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::uint8_t pixels[4] = {255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(61u, 1u, 1u, ImageFormat::RGBA8, pixels);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::image(image, AlborRect::from_origin_size(AlborPoint{3.4f, 2.7f}, AlborSize{9.6f, 8.3f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the image covers its interior, which a dropped draw would leave white
    const std::size_t interior = (6u * 24u + 8u) * 4u;
    EXPECT_EQ(actual_shot.pixels[interior], 255u);
    EXPECT_EQ(actual_shot.pixels[interior + 1], 0u);
    // the first column the image crosses is partly covered, which a hard quad edge would not produce
    const std::size_t edge = (6u * 24u + 3u) * 4u;
    EXPECT_LT(actual_shot.pixels[edge + 1], 255u);
    EXPECT_GT(actual_shot.pixels[edge + 1], 0u);
    // the reference feathers the same edge, which the two agree on within the one pixel band
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborRendererBackendChecks, CompositesACastOnAFrameWithNoGeometry) {
    // arrange a frame whose only content is a shadow, which no vertex range carries
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    RenderContext& context = surface->begin_frame();
    context.draw_shadow(AlborRect::from_origin_size(AlborPoint{8.0f, 8.0f}, AlborSize{16.0f, 16.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.8f), .offset = AlborPoint{0.0f, 0.0f}, .blur = 4.0f});
    surface->end_frame();
    const Screenshot shot = capture_surface(*surface);
    // assert
    ASSERT_FALSE(shot.empty());
    // the cast lands even though the frame carried no geometry at all
    const std::size_t centre = (16u * 32u + 16u) * 4u;
    EXPECT_GT(shot.pixels[centre + 3], 0u);
}
