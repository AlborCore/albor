#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#import <QuartzCore/CAMetalLayer.h>

#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
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

namespace
{
    // read a surface back into a screenshot, which the parity comparison consumes (§6.3, AD-26)
    Screenshot capture(const RenderSurface& surface) {
        // the surface hands out the canonical bytes a screenshot wraps (§6.3)
        const std::vector<std::uint8_t> pixels = surface.read_pixels();
        // the extent comes from the metrics, which is the physical pixel size (§6.3, AD-27)
        Screenshot shot;
        shot.width = static_cast<int>(surface.metrics().pixel_size.width);
        shot.height = static_cast<int>(surface.metrics().pixel_size.height);
        shot.pixels = pixels;
        return shot;
    }

    // count the pixels whose worst channel differs by more than the tolerance (§6.3, AD-26)
    std::size_t differing_pixels(const Screenshot& lhs, const Screenshot& rhs, int tolerance) {
        // a size mismatch compares every pixel it can, which a failing test reports separately
        const std::size_t count = std::min(lhs.pixels.size(), rhs.pixels.size()) / 4u;
        std::size_t differing = 0;
        for (std::size_t index = 0; index < count; index++) {
            // the worst channel of the pixel decides whether it is outside the tolerance
            int worst = 0;
            for (std::size_t channel = 0; channel < 4; channel++)
                worst = std::max(worst, std::abs(static_cast<int>(lhs.pixels[index * 4 + channel]) - static_cast<int>(rhs.pixels[index * 4 + channel])));
            if (worst > tolerance)
                differing++;
        }
        return differing;
    }
} // namespace

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
    const Screenshot expected = capture(*expected_surface);
    const Screenshot actual = capture(*actual_surface);
    // assert
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    // an aligned rectangle has no feathered edge to disagree on, so the two agree exactly
    EXPECT_EQ(differing_pixels(expected, actual, 0), 0u);
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
    const Screenshot expected = capture(*expected_surface);
    const Screenshot actual = capture(*actual_surface);
    // assert
    const std::size_t differing = differing_pixels(expected, actual, 24);
    EXPECT_EQ(differing, 0u);
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
    const Screenshot expected = capture(*expected_surface);
    const Screenshot actual = capture(*actual_surface);
    // assert
    ASSERT_EQ(expected.pixels.size(), 32u * 32u * 4u);
    EXPECT_EQ(differing_pixels(expected, actual, 0), 0u);
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
    const Screenshot expected = capture(*expected_surface);
    const Screenshot actual = capture(*actual_surface);
    // assert
    EXPECT_EQ(differing_pixels(expected, actual, 24), 0u);
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
    const Screenshot expected_shot = capture(*expected_surface);
    const Screenshot actual_shot = capture(*actual_surface);
    // assert
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
    const Screenshot first = capture(*surface);
    backend.execute_frame(*surface, scene);
    const Screenshot second = capture(*surface);
    // assert
    ASSERT_EQ(first.pixels.size(), second.pixels.size());
    EXPECT_EQ(differing_pixels(first, second, 0), 0u);
    // the image paints blue over the black background, which the upload preserved
    EXPECT_EQ(second.pixels[0], 0u);
    EXPECT_EQ(second.pixels[2], 255u);
}
