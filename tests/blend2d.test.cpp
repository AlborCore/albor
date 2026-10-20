#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/native_window_handle.h"
#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/render-blend2d/blend2d_render_context.h"
#include "render/render-blend2d/blend2d_render_surface.h"
#include "render/render-blend2d/blend2d_renderer_backend.h"
#include "render/render-blend2d/blend2d_window_surface.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/resource_factory.h"
#include "render/screenshot.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "ui/scene_builder.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"

using namespace albor;
using namespace albor::blend2d;

namespace
{
    // the logical width every scene is drawn into, which a cpu raster needs no window server for (§6.3)
    constexpr float SCENE_WIDTH = 64.0f;

    // the logical height every scene is drawn into, which is deep enough for a small canvas of shapes (§6.3)
    constexpr float SCENE_HEIGHT = 48.0f;

    // the channel distance antialiased edges are allowed, warmed past the 52 coverage gamma keeps apart (§6.3, AD-26)
    constexpr int SHAPE_PARITY_TOLERANCE = 64;

    // the channel distance the two shadow blurs are allowed, over the measured 43 a box and a gaussian keep apart (§6.2)
    constexpr int SHADOW_PARITY_TOLERANCE = 64;

    // the channel distance two glyph rasterizers may differ, measured at 161 with the vendored platform face (§9.1, §16)
    constexpr int TEXT_PARITY_TOLERANCE = 192;

    // the metrics every scene presents and every surface creates against, at whole pixels (§6.1, AD-27)
    WindowMetrics scene_metrics(float scale_factor) {
        // the logical size is the extent the scene spans, which the scale maps onto whole pixels (§6.3, AD-27)
        return WindowMetrics::from_logical(Size{SCENE_WIDTH, SCENE_HEIGHT}, scale_factor);
    }

    // add one recorded command to a scene under the whole surface, which is what stage one stood at (§6.1, §6.2)
    void record(FrameScene& scene, const WindowMetrics& metrics, DrawCommand command) {
        // the whole surface is the clip the replay scissors to, because nothing narrows it in these scenes (§6.2)
        command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        // the command joins the scene in paint order, which is how the engines replay it (§6.1)
        scene.add_command(std::move(command));
    }

    // the rectangle parity scene, whose edges land on whole pixels so every byte must agree (§6.3, AD-26)
    FrameScene rectangle_scene(const WindowMetrics& metrics) {
        // the scene is cleared to black, which both engines fill with whole pixels (§6.1)
        FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
        // the red rectangle sits inside the extent, so its edges never antialias (§6.3, AD-26)
        record(scene, metrics, DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)}));
        // the white rectangle sits inside the extent at a distance, so a second fill is pinned too (§6.3, AD-26)
        record(scene, metrics, DrawCommand::rectangle(Rect::from_origin_size(Point{32.0f, 24.0f}, Size{16.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}));
        return scene;
    }

    // the shape parity scene, where antialiased edges decide how close two rasterizers can be (§6.3, AD-26)
    FrameScene shape_scene(const WindowMetrics& metrics, const ResourceFactory& factory) {
        // the scene is cleared to black, which both engines fill with whole pixels (§6.1)
        FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
        // the rounded rectangle carries the corners one engine may soften more than the other (§6.2)
        record(scene, metrics, DrawCommand::rounded_rectangle(Rect::from_origin_size(Point{24.0f, 4.0f}, Size{16.0f, 10.0f}), 4.0f, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)}));
        // the polyline carries a diagonal, which is the stroke both engines soften (§6.2)
        const std::array<Point, 3> bend{Point{4.0f, 16.0f}, Point{28.0f, 28.0f}, Point{48.0f, 12.0f}};
        record(scene, metrics, DrawCommand::polyline(std::span<const Point>(bend), StrokeStyle{.color = Color::rgb(0.0f, 0.0f, 1.0f), .width = 2.0f}));
        // the triangle path carries three diagonal contours, which is the retained draw both engines replay (§6.4)
        const std::array<Point, 3> tri{Point{6.0f, 34.0f}, Point{22.0f, 42.0f}, Point{6.0f, 44.0f}};
        const std::array<PathVerb, 4> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
        const std::shared_ptr<const PathResource> path = factory.create_path(std::span<const PathVerb>(verbs), std::span<const Point>(tri));
        record(scene, metrics, DrawCommand::path(path, Transform{}, Paint{.fill = Color::rgb(1.0f, 0.5f, 0.0f)}));
        // the image is opaque and whole, so its interior must agree byte for byte (§6.4, AD-26)
        const std::vector<std::uint8_t> pixels(8 * 8 * 4, 0xff);
        const std::shared_ptr<const ImageResource> image = factory.create_image(8, 8, ImageFormat::RGBA8, pixels);
        record(scene, metrics, DrawCommand::image(image, Rect::from_origin_size(Point{40.0f, 32.0f}, Size{8.0f, 8.0f}), Paint{}));
        // the cast is hard and axis aligned, skipping the soft edge both blurs disagree on (§6.2)
        record(scene, metrics, DrawCommand::shadow(Rect::from_origin_size(Point{26.0f, 26.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f), .offset = Point{2.0f, 2.0f}, .blur = 0.0f}));
        return scene;
    }

    // the soft shadow parity scene, whose falloff is where the box and the gaussian differ most (§6.2, §12)
    FrameScene shadow_scene(const WindowMetrics& metrics) {
        // the scene is cleared to black, so the cast falls onto a whole pixel background (§6.1)
        FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
        // the soft cast keeps a reach inside the extent, so its blur never clips the surface (§6.2)
        record(scene, metrics, DrawCommand::shadow(Rect::from_origin_size(Point{16.0f, 12.0f}, Size{16.0f, 12.0f}), 0.0f, Shadow{.color = Color::rgba(1.0f, 1.0f, 1.0f, 0.8f), .offset = Point{4.0f, 4.0f}, .blur = 3.0f}));
        return scene;
    }

    // the test double that draws one measured run through the ordinary text call, which is what a label does (§6.1)
    class TextProbeNode final : public UiNode
    {
    public:
        // override: draw the run at the node origin, which is the ordinary text call every node makes (§6.1)
        void render(RenderContext& context) override {
            // a run that measured nothing draws nothing, because it carries no glyph (§6.2)
            if (m_layout.run == nullptr)
                return;
            // the fill is white, because the probe scene clears to black for the contrast (§6.2)
            context.draw_text(m_layout, Point{bounds().x, bounds().y}, {.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
            // the children draw over it, which is the composite contract of a node (§6.1)
            UiNode::render(context);
        }

        // the run this node draws, which the test measures with the platform engine (§9.1)
        TextLayout m_layout;
    };
} // namespace

TEST(Blend2DRenderSurfaceChecks, MetricsWithNoAreaAreRefusedAtCreation) {
    // act an extent whose physical area is zero cannot be rasterized, so the refusal returns no surface (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(WindowMetrics::from_logical(Size{SCENE_WIDTH, SCENE_HEIGHT}, 0.0f));
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(Blend2DRenderSurfaceChecks, PixelsBeforeTheFirstFrameAreRefused) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert a surface that finished nothing reports no pixels, because a read is of the last frame (§6.3)
    EXPECT_TRUE(pixels.empty());
}

TEST(Blend2DRenderSurfaceChecks, PixelsMidFrameAreRefusedUntilTheFrameEnds) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act
    (void)surface->begin_frame();
    // assert a frame in flight is not finished, so its pixels refuse the read (§6.1, §6.3)
    EXPECT_TRUE(surface->read_pixels().empty());
    // act the frame is finished, which is what makes the pixels readable (§6.1)
    surface->end_frame();
    // assert
    EXPECT_FALSE(surface->read_pixels().empty());
}

TEST(Blend2DRenderSurfaceChecks, AFinishedFrameReadsBackInCanonicalOrder) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act one red rectangle over an unscaled extent, which the read back walks (§6.3)
    RenderContext& context = surface->begin_frame();
    context.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    surface->end_frame();
    // assert the read is tightly packed rgba, transparent at the corner and red inside the rectangle (§6.3, AD-26)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), 64u * 48u * 4u);
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[1], 0u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 0u);
    const std::size_t red = (12u * 64u + 14u) * 4u;
    EXPECT_EQ(pixels[red], 255u);
    EXPECT_EQ(pixels[red + 1u], 0u);
    EXPECT_EQ(pixels[red + 2u], 0u);
    EXPECT_EQ(pixels[red + 3u], 255u);
}

TEST(Blend2DRenderSurfaceChecks, FramesCountOnlyTheBalancedEnds) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act an end with no begin which is what an unbalanced caller could try (§6.1)
    surface->end_frame();
    // assert the surface counts nothing, because no frame began (§6.1)
    EXPECT_EQ(surface->frame_count(), 0u);
    // act two balanced frames, which is what a real pipeline runs (§6.1)
    (void)surface->begin_frame();
    surface->end_frame();
    (void)surface->begin_frame();
    surface->end_frame();
    // assert
    EXPECT_EQ(surface->frame_count(), 2u);
}

TEST(Blend2DRenderSurfaceChecks, AResizeChangesTheMetricsTheSurfacePresentsAt) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act the resize keeps the surface valid against the new extent (§6.2)
    surface->resize(scene_metrics(2.0f));
    // assert
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics().scale_factor, 2.0f);
    EXPECT_EQ(surface->metrics().pixel_size.width, 128u);
}

TEST(Blend2DRenderContextChecks, ASaveCarriesTheStateBackToWhereItStood) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.save();
    EXPECT_EQ(context.opacity(), 1.0f);
    context.set_transform(Transform::translation(6.0f, 8.0f));
    context.clip_rect(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{20.0f, 10.0f}));
    context.set_opacity(0.5f);
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{64.0f, 48.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    context.restore();
    surface->end_frame();
    // assert the state stack is empty and reads the frame state again, because a restore is the matching pop (§6.2)
    const auto& blend_context = static_cast<Blend2DRenderContext&>(context);
    EXPECT_EQ(blend_context.depth(), 0u);
    EXPECT_EQ(context.transform(), Transform{});
    EXPECT_EQ(context.clip(), Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}));
    EXPECT_FLOAT_EQ(context.opacity(), 1.0f);
}

TEST(Blend2DRenderContextChecks, AClipIntersectsTheOneThatWasStanding) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act two clips over each other, which is what a nested layout does (§6.2)
    context.clip_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 12.0f}));
    context.clip_rect(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    surface->end_frame();
    // assert the read back clip is the intersection, because a clip narrows rather than replaces (§6.2)
    const Rect narrowed = context.clip();
    EXPECT_FLOAT_EQ(narrowed.x, 10.0f);
    EXPECT_FLOAT_EQ(narrowed.y, 10.0f);
    EXPECT_FLOAT_EQ(narrowed.width, 14.0f);
    EXPECT_FLOAT_EQ(narrowed.height, 6.0f);
}

TEST(Blend2DRenderContextChecks, ARoundedClipLeavesItsBoundingRectangleStanding) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.clip_rounded_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 12.0f}), 4.0f);
    surface->end_frame();
    // assert blend2d clips to rectangles only, so the recorded clip is the bounding one (§6.2, AD-26)
    const Rect narrowed = context.clip();
    EXPECT_FLOAT_EQ(narrowed.x, 4.0f);
    EXPECT_FLOAT_EQ(narrowed.width, 20.0f);
}

TEST(Blend2DRenderContextChecks, TheOpacityIsClampedIntoTheUnitRange) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act the extremes content bindings can produce, which the clamp must fold into the unit range (§6.1)
    context.set_opacity(9.0f);
    // assert
    EXPECT_FLOAT_EQ(context.opacity(), 1.0f);
    // act
    context.set_opacity(-1.0f);
    // assert
    EXPECT_FLOAT_EQ(context.opacity(), 0.0f);
    // act
    surface->end_frame();
}

TEST(Blend2DRenderContextChecks, AContextWithNoRasterSurvivesEveryDraw) {
    // arrange a context over no raster at all, which is what an unusable surface hands out (§6.2)
    Blend2DRenderContext context(nullptr, 1.0f, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}));
    ResourceFactory factory;
    const std::array<Point, 3> bend{Point{1.0f, 1.0f}, Point{5.0f, 1.0f}, Point{5.0f, 5.0f}};
    const std::array<PathVerb, 3> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo};
    const std::shared_ptr<const PathResource> path = factory.create_path(std::span<const PathVerb>(verbs), std::span<const Point>(bend));
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(std::span<const Point>(bend));
    const std::vector<std::uint8_t> pixels(4 * 4 * 4, 0xff);
    const std::shared_ptr<const ImageResource> image = factory.create_image(4, 4, ImageFormat::RGBA8, pixels);
    const TextLayout layout{.run = ShapedRun::make({}, 16.0f), .size = Size{10.0f, 10.0f}};
    // act every draw a caller could make, because each one must survive the null raster (§6.2)
    context.save();
    context.set_transform(Transform::translation(2.0f, 2.0f));
    context.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    context.set_opacity(0.5f);
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 2.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f), .blur = 2.0f});
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 2.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.draw_polyline(std::span<const Point>{}, StrokeStyle{});
    context.draw_line_batch(batch, Transform{}, StrokeStyle{});
    context.draw_path(path, Transform{}, Paint{});
    context.draw_image(image, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f}), Paint{});
    context.draw_text(layout, Point{0.0f, 0.0f}, Paint{});
    // assert the state machine stayed usable, because a null raster is a refusal rather than a crash (§6.2)
    EXPECT_EQ(context.depth(), 1u);
    EXPECT_EQ(context.transform(), Transform::translation(2.0f, 2.0f));
    // act the stack is balanced back, which the surface replay would do (§6.2)
    context.restore();
    // assert
    EXPECT_EQ(context.depth(), 0u);
}

TEST(Blend2DRendererBackendChecks, TheBackendIsAlwaysReadyAndBuildsASurface) {
    // arrange
    Blend2DRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    // assert the cpu raster needs no device and no handle, so it is ready and builds (§6.3, AD-26)
    EXPECT_TRUE(backend.ready());
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
}

TEST(Blend2DRendererBackendChecks, AFrameThroughTheBackendReadsBackInk) {
    // arrange
    Blend2DRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act
    backend.execute_frame(*surface, rectangle_scene(scene_metrics(1.0f)));
    // assert the frame is finished and read back, which is what stage two produces (§6.1, §6.3)
    EXPECT_EQ(static_cast<Blend2DRenderSurface*>(surface.get())->frame_count(), 1u);
    const Screenshot screenshot = capture_surface(*surface);
    EXPECT_FALSE(screenshot.empty());
}

TEST(Blend2DRendererBackendChecks, AFrameAtStaleMetricsIsRefused) {
    // arrange
    Blend2DRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act a scene at double the metrics, which is what a stale scene looks like (§6.1, D9.1)
    backend.execute_frame(*surface, rectangle_scene(scene_metrics(2.0f)));
    // assert the frame never ran, so the read back refuses rather than producing a half picture (§6.1, D9.1)
    EXPECT_EQ(static_cast<Blend2DRenderSurface*>(surface.get())->frame_count(), 0u);
    EXPECT_TRUE(capture_surface(*surface).empty());
}

TEST(Blend2DRendererBackendChecks, AResizeIsFollowedByFramesAtTheNewMetrics) {
    // arrange
    Blend2DRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    // act
    backend.resize_surface(*surface, scene_metrics(2.0f));
    backend.execute_frame(*surface, rectangle_scene(scene_metrics(2.0f)));
    // assert the frame ran at the resized extent, which is what a window resize produces (§6.2, D9.1)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_NE(screenshot.width, 0);
    EXPECT_EQ(screenshot.width, 128);
    EXPECT_EQ(screenshot.height, 96);
}

TEST(Blend2DParityChecks, AnAlignedRectangleSceneMatchesTheReferenceBytewise) {
    // arrange the scene is drawn once through each engine, what a parity check does (§6.3, AD-26)
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    const WindowMetrics metrics = scene_metrics(1.0f);
    const FrameScene scene = rectangle_scene(metrics);
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert both engines read a picture back, because an empty read would agree trivially (§6.3, AD-26)
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert whole pixels must agree byte for byte, which the aligned edges of the scene make possible (§6.3, AD-26)
    EXPECT_EQ(reference_read.max_channel_difference(measured_read), 0) << "engines diverged by more than whole pixels allow";
}

TEST(Blend2DParityChecks, AnAlignedRectangleSceneMatchesTheReferenceAtDoubleScale) {
    // arrange the scene is drawn at a non unit scale, which is what pins the meta matrix (§12, AD-27)
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    const WindowMetrics metrics = scene_metrics(2.0f);
    const FrameScene scene = rectangle_scene(metrics);
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert a logical rectangle at that scale maps onto whole pixels, which the matrix composition does (§12, AD-27)
    EXPECT_EQ(reference_read.max_channel_difference(measured_read), 0) << "the meta matrix composition disagrees with the reference";
}

TEST(Blend2DParityChecks, AShapeCorpusStaysWithinTheAntialiasedTolerance) {
    // arrange
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    ResourceFactory factory;
    const WindowMetrics metrics = scene_metrics(1.0f);
    const FrameScene scene = shape_scene(metrics, factory);
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert the antialiased edges stay in tolerance, which the shared draw contract narrows them to (§6.3, AD-26)
    const int difference = reference_read.max_channel_difference(measured_read);
    EXPECT_LE(difference, SHAPE_PARITY_TOLERANCE) << "shapes diverged by " << difference << " per channel";
}

TEST(Blend2DParityChecks, ASoftShadowStaysWithinTheBlurTolerance) {
    // arrange
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    const WindowMetrics metrics = scene_metrics(1.0f);
    const FrameScene scene = shadow_scene(metrics);
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert the falloffs stay in tolerance, since a box and a gaussian agree in the middle and at the reach (§6.2)
    const int difference = reference_read.max_channel_difference(measured_read);
    EXPECT_LE(difference, SHADOW_PARITY_TOLERANCE) << "the blurs diverged by " << difference << " per channel";
}

TEST(Blend2DParityChecks, ATranslucentImageMatchesTheReference) {
    // arrange both engines composite a premultiplied half covered white image over red (§6.4, AD-26)
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    ResourceFactory factory;
    const std::vector<std::uint8_t> pixels(16 * 16 * 4, 128);
    const std::shared_ptr<const ImageResource> image = factory.create_image(16, 16, ImageFormat::RGBA8, pixels);
    const WindowMetrics metrics = scene_metrics(1.0f);
    FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    record(scene, metrics, DrawCommand::image(image, Rect::from_origin_size(Point{24.0f, 16.0f}, Size{16.0f, 16.0f}), Paint{}));
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert the premultiplied bytes composite the same, because both uploads assume one alpha contract (§6.4, AD-26)
    const int difference = reference_read.max_channel_difference(measured_read);
    EXPECT_LE(difference, 2) << "the translucent image diverged by " << difference << " per channel";
}

TEST(Blend2DParityChecks, AnImageUnderADimmedPaintMatchesTheReference) {
    // arrange an opaque white image drawn with a half opaque fill, which the alpha modulation dims (§6.4, §6.1)
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    ResourceFactory factory;
    const std::vector<std::uint8_t> pixels(16 * 16 * 4, 0xff);
    const std::shared_ptr<const ImageResource> image = factory.create_image(16, 16, ImageFormat::RGBA8, pixels);
    const WindowMetrics metrics = scene_metrics(1.0f);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    record(scene, metrics, DrawCommand::image(image, Rect::from_origin_size(Point{24.0f, 16.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)}));
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert the paint alpha modulates the upload once, which is what the reference draw does (§6.4, §6.1)
    const int difference = reference_read.max_channel_difference(measured_read);
    EXPECT_LE(difference, 2) << "the dimmed image diverged by " << difference << " per channel";
}

TEST(Blend2DRenderContextChecks, AnOpacityDimsTheReadbackBytes) {
    // arrange
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act a half opaque white fill, which the opacity folds into the paint colour once (§6.1)
    context.set_opacity(0.5f);
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    surface->end_frame();
    // assert the read back pixel is the dimmed white, whose channels truncate at 127 (§6.1, §6.3)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    const std::size_t pixel = (10u * 64u + 10u) * 4u;
    EXPECT_EQ(screenshot.pixels[pixel + 0u], 127u);
    EXPECT_EQ(screenshot.pixels[pixel + 1u], 127u);
    EXPECT_EQ(screenshot.pixels[pixel + 2u], 127u);
    EXPECT_EQ(screenshot.pixels[pixel + 3u], 127u);
}

TEST(Blend2DRendererBackendInstallChecks, TheInstallFunctionReplacesTheGlobalFactory) {
    // arrange a blank slate, which makes the install observable (§6.3)
    set_renderer_backend_factory(nullptr);
    // act
    install_blend2d_renderer_backend();
    const std::unique_ptr<RendererBackend> backend = create_renderer_backend();
    // assert the factory hands out the blend2d backend, which is always ready (§6.3, AD-26)
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->ready());
    const std::unique_ptr<RenderSurface> surface = backend->create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    // act the factory is cleared, because the global state is process wide (§6.3)
    set_renderer_backend_factory(nullptr);
}

TEST(Blend2DWindowSurfaceRoutingChecks, AValidHandleRoutesThroughTheInstalledWindowSurfaceFactory) {
    // arrange a factory that hands a real raster back, which stands in for the platform half (§5.3, AD-16)
    bool invoked = false;
    RenderSurface* handed_back = nullptr;
    set_window_surface_factory([&](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> {
        (void)handle;
        invoked = true;
        std::unique_ptr<RenderSurface> surface = Blend2DRenderSurface::create(metrics);
        handed_back = surface.get();
        return surface;
    });
    Blend2DRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(&invoked), scene_metrics(1.0f));
    // assert the window handle reached the factory and its surface is the one the caller got (§5.3, AD-16)
    EXPECT_TRUE(invoked);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface.get(), handed_back);
    // act the hook is restored, because the factory state is process wide (§5.3)
    set_window_surface_factory(nullptr);
}

TEST(Blend2DWindowSurfaceRoutingChecks, AnInvalidHandleNeverReachesTheWindowSurfaceFactory) {
    // arrange a factory that would fail the test if a headless request ever consulted it (§5.3)
    bool invoked = false;
    set_window_surface_factory([&](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> {
        (void)handle;
        (void)metrics;
        invoked = true;
        return nullptr;
    });
    Blend2DRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, scene_metrics(1.0f));
    // assert the plain raster answered, because only a windowed host consults the platform half (§5.3, §6.3)
    EXPECT_FALSE(invoked);
    ASSERT_NE(surface, nullptr);
    EXPECT_NE(dynamic_cast<Blend2DRenderSurface*>(surface.get()), nullptr);
    // act the hook is restored, because the factory state is process wide (§5.3)
    set_window_surface_factory(nullptr);
}

TEST(Blend2DWindowSurfaceRoutingChecks, AnEmptyFactoryFallsBackToThePlainRaster) {
    // arrange the hook starts empty in a test process, because no platform half is linked here (§6.3, AD-26)
    set_window_surface_factory(nullptr);
    Blend2DRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(&backend), scene_metrics(1.0f));
    // assert the documented raster fallback is what a build without a window half presents into (§6.3, AD-26)
    ASSERT_NE(surface, nullptr);
    EXPECT_NE(dynamic_cast<Blend2DRenderSurface*>(surface.get()), nullptr);
}

TEST(Blend2DWindowSurfaceRoutingChecks, ARefusedWindowSurfaceStaysRefused) {
    // arrange a factory that refuses, which is what a foreign handle looks like to the platform half (§5.3)
    set_window_surface_factory([](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> {
        (void)handle;
        (void)metrics;
        return nullptr;
    });
    Blend2DRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(&backend), scene_metrics(1.0f));
    // assert the refusal surfaces, because a silent raster fallback would draw a blank window (§5.3, §6.3)
    EXPECT_EQ(surface, nullptr);
    // act the hook is restored, because the factory state is process wide (§5.3)
    set_window_surface_factory(nullptr);
}

TEST(Blend2DWindowSurfaceRoutingChecks, ARoutedSurfaceExecutesFramesThroughTheBackend) {
    // arrange the factory hands a real raster back, which is what a windowed host draws through (§6.1, §6.3)
    set_window_surface_factory([](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> {
        (void)handle;
        return Blend2DRenderSurface::create(metrics);
    });
    Blend2DRendererBackend backend;
    const WindowMetrics metrics = scene_metrics(1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(&backend), metrics);
    ASSERT_NE(surface, nullptr);
    // act
    backend.execute_frame(*surface, rectangle_scene(metrics));
    // assert the routed surface drew and read back the frame, which is the windowed pipeline (§6.1, §6.3)
    EXPECT_EQ(static_cast<Blend2DRenderSurface*>(surface.get())->frame_count(), 1u);
    EXPECT_FALSE(capture_surface(*surface).empty());
    // act the hook is restored, because the factory state is process wide (§5.3)
    set_window_surface_factory(nullptr);
}

TEST(Blend2DTextChecks, APlatformShapedRunCarriesARegisteredFontIdentity) {
    // arrange the platform engine shapes the way a windowed application does (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->ready());
    // act
    const TextLayout layout = engine->measure("Albor", TextEngine::UNBOUNDED_WIDTH, 16.0f);
    // assert the run is stamped with a hash, because a replaying backend resolves its font by it (§15, AD-34)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_FALSE(layout.run->empty());
    EXPECT_NE(layout.run->font_hash(), 0u);
    EXPECT_TRUE(resources::FontRegistry::find_hash(layout.run->font_hash()) != nullptr);
}

TEST(Blend2DTextChecks, APlatformShapedRunDrawsInkThroughTheBackend) {
    // arrange the sentence a label draws, shaped by the platform engine and replayed by blend2d (§9.1, §6.2)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->ready());
    const TextLayout layout = engine->measure("Hello", TextEngine::UNBOUNDED_WIDTH, 16.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_FALSE(layout.run->empty());
    // the run resolved its font in the registry, which is what a replaying backend loads from (§15, AD-34)
    const std::shared_ptr<const resources::EmbeddedFont> embedded = resources::FontRegistry::find_hash(layout.run->font_hash());
    ASSERT_NE(embedded, nullptr);
    ASSERT_FALSE(embedded->bytes.empty());
    const WindowMetrics metrics = scene_metrics(1.0f);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    record(scene, metrics, DrawCommand::text(layout, Point{8.0f, 16.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}));
    Blend2DRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    backend.execute_frame(*surface, scene);
    // assert the glyphs left ink on the raster, because an unresolved font would leave plain background (§6.2)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        // a pixel darker than the white background is where a glyph put its coverage down (§6.2)
        if (screenshot.pixels[index] < 200u)
            ink++;
    }
    EXPECT_GT(ink, 0u) << "the shaped text drew no ink at all";
}

TEST(Blend2DTextChecks, AShapedRunRendersLikeTheReference) {
    // arrange the same shaped layout through both engines, which is what text parity checks (§16, AD-26)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->ready());
    const TextLayout layout = engine->measure("Hello", TextEngine::UNBOUNDED_WIDTH, 16.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_FALSE(layout.run->empty());
    const WindowMetrics metrics = scene_metrics(1.0f);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    record(scene, metrics, DrawCommand::text(layout, Point{8.0f, 16.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}));
    software::SoftwareRendererBackend reference_backend;
    Blend2DRendererBackend measured_backend;
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> measured = measured_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(measured, nullptr);
    // act
    reference_backend.execute_frame(*reference, scene);
    measured_backend.execute_frame(*measured, scene);
    const Screenshot reference_read = capture_surface(*reference);
    const Screenshot measured_read = capture_surface(*measured);
    // assert
    ASSERT_FALSE(reference_read.empty());
    ASSERT_FALSE(measured_read.empty());
    // assert each engine put ink down, because vanishing glyphs are exactly the failure this guards (§16, §9.1)
    std::size_t reference_ink = 0;
    std::size_t measured_ink = 0;
    for (std::size_t index = 0; index < reference_read.pixels.size(); index += 4) {
        if (reference_read.pixels[index] < 200u)
            reference_ink++;
    }
    for (std::size_t index = 0; index < measured_read.pixels.size(); index += 4) {
        if (measured_read.pixels[index] < 200u)
            measured_ink++;
    }
    EXPECT_GT(reference_ink, 0u) << "the reference drew no ink";
    EXPECT_GT(measured_ink, 0u) << "the measured backend drew no ink";
    // assert the two silhouettes cover about the same area, which a misplaced or wrong glyph set would break (§16)
    const std::size_t larger_ink = std::max(reference_ink, measured_ink);
    const std::size_t smaller_ink = std::min(reference_ink, measured_ink);
    EXPECT_GE(smaller_ink * 4u, larger_ink * 3u) << "the two glyph silhouettes cover different areas";
    // assert the glyph rasterizations stay in tolerance, which the shared shaped input narrows them to (§16, AD-26)
    const int difference = reference_read.max_channel_difference(measured_read);
    EXPECT_LE(difference, TEXT_PARITY_TOLERANCE) << "the shaped text diverged by " << difference << " per channel";
}

TEST(Blend2DTextChecks, APlatformRunBuiltIntoASceneDrawsInkThroughTheBackend) {
    // arrange a run shaped by the platform engine and built through the ordinary scene builder (§9.1, §6.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->ready());
    TextProbeNode node;
    node.m_layout = engine->measure("Albor", TextEngine::UNBOUNDED_WIDTH, 20.0f);
    ASSERT_NE(node.m_layout.run, nullptr);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f);
    SceneBuilder builder;
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    Blend2DRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, *scene);
    // assert the platform run reached the raster as ink, which is the whole chain a label depends on (§6.2, §15)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        // the fill is white over a black clear, so a lit channel is where a glyph landed (§6.2)
        if (screenshot.pixels[index] > 200u)
            ink++;
    }
    EXPECT_GT(ink, 0u) << "the platform run drew no ink through the scene builder";
}

TEST(Blend2DRenderContextChecks, APictureReplaysItsCommandsIntoTheRaster) {
    // arrange a picture of one white rectangle, which a retained view records off the ui thread (§6.4)
    ResourceFactory factory;
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    ASSERT_NE(picture, nullptr);
    ASSERT_FALSE(picture->empty());
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_picture(picture, Transform{});
    surface->end_frame();
    // assert the picture's command reached the raster, which is what a retained view replays (§6.4)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    const std::size_t pixel = (10u * 64u + 10u) * 4u;
    EXPECT_EQ(screenshot.pixels[pixel + 3u], 255u);
}

TEST(Blend2DRenderContextChecks, AStrokedPathDrawsInk) {
    // arrange a triangle contour drawn with a stroke and no fill, which a chart line is (§6.4)
    ResourceFactory factory;
    const std::array<Point, 3> tri{Point{6.0f, 6.0f}, Point{30.0f, 6.0f}, Point{18.0f, 24.0f}};
    const std::array<PathVerb, 4> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::shared_ptr<const PathResource> path = factory.create_path(std::span<const PathVerb>(verbs), std::span<const Point>(tri));
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_path(path, Transform{}, Paint{.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = Color::rgb(1.0f, 1.0f, 1.0f), .stroke_width = 2.0f});
    surface->end_frame();
    // assert the stroke left ink, because a stroked contour is a first class draw (§6.4)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        if (screenshot.pixels[index] > 200u)
            ink++;
    }
    EXPECT_GT(ink, 0u);
}

TEST(Blend2DRenderContextChecks, AnEvenOddPathCutsASameWoundHole) {
    // arrange two nested squares of one winding, whose hole only the even odd rule cuts (§6.2)
    ResourceFactory factory;
    const std::array<Point, 8> points{Point{8.0f, 8.0f}, Point{8.0f, 32.0f}, Point{32.0f, 32.0f}, Point{32.0f, 8.0f}, Point{16.0f, 16.0f}, Point{16.0f, 24.0f}, Point{24.0f, 24.0f}, Point{24.0f, 16.0f}};
    const std::array<PathVerb, 10> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::shared_ptr<const PathResource> path = factory.create_path(std::span<const PathVerb>(verbs), std::span<const Point>(points));
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act the even odd fill paints the band and leaves the nested ring open
    context.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f), .fill_rule = FillRule::EvenOdd});
    surface->end_frame();
    // assert the band carries ink while the hole stays clear, which is the parity the rule decides
    const Screenshot covered = capture_surface(*surface);
    ASSERT_FALSE(covered.empty());
    const std::size_t band = (12u * 64u + 12u) * 4u;
    const std::size_t hole = (20u * 64u + 20u) * 4u;
    EXPECT_EQ(covered.pixels[band + 3u], 255u);
    EXPECT_EQ(covered.pixels[hole + 3u], 0u);
    RenderContext& solid = surface->begin_frame();
    // act the default non zero rule fills the same winding solid, which the rule alone decides
    solid.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    surface->end_frame();
    // assert the hole fills under the default rule, which proves the even odd paint carried the rule
    const Screenshot filled = capture_surface(*surface);
    ASSERT_FALSE(filled.empty());
    EXPECT_EQ(filled.pixels[hole + 3u], 255u);
}

TEST(Blend2DRenderContextChecks, EveryCompositionModeLeavesTheOverlapItsOperatorDefines) {
    // arrange one red base and one blue overlay per composition mode, whose overlap the operator alone decides (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    const CompositionMode modes[11] = {CompositionMode::SourceOver, CompositionMode::SourceIn, CompositionMode::SourceOut, CompositionMode::Atop, CompositionMode::DestinationOver, CompositionMode::DestinationIn, CompositionMode::DestinationOut, CompositionMode::DestinationAtop, CompositionMode::Lighter, CompositionMode::Copy, CompositionMode::Xor};
    const std::uint8_t expected[11][4] = {{51, 51, 204, 255}, {51, 51, 204, 255}, {0, 0, 0, 0}, {51, 51, 204, 255}, {204, 51, 51, 255}, {204, 51, 51, 255}, {0, 0, 0, 0}, {204, 51, 51, 255}, {255, 102, 255, 255}, {51, 51, 204, 255}, {0, 0, 0, 0}};
    for (std::size_t index = 0; index < 11; index++) {
        // the trace names the mode a mismatch belongs to, which a loop otherwise hides
        SCOPED_TRACE(static_cast<int>(modes[index]));
        RenderContext& context = surface->begin_frame();
        // act the base paints first and the overlay blends over it under the mode
        context.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color{.red = 0.8f, .green = 0.2f, .blue = 0.2f}});
        context.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f}, .composition = modes[index]});
        surface->end_frame();
        // assert the overlap reads back the bytes the operator defines, which the blender must match
        const Screenshot overlap = capture_surface(*surface);
        ASSERT_FALSE(overlap.empty());
        const std::size_t pixel = (16u * 64u + 16u) * 4u;
        for (std::size_t channel = 0; channel < 4; channel++)
            EXPECT_NEAR(overlap.pixels[pixel + channel], expected[index][channel], 2) << "channel " << channel;
    }
}

TEST(Blend2DRenderContextChecks, ALayerDimsOverlappingChildrenOnce) {
    // arrange a white plate and a half opaque group whose two red children overlap by design (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    const Paint red{.fill = Color::rgb(0.8f, 0.2f, 0.2f)};
    // act the group paints both children opaque, then composites once at half opacity
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.push_layer(0.5f);
    context.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{24.0f, 24.0f}), red);
    context.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), red);
    context.pop_layer();
    surface->end_frame();
    // assert the overlap matches the single covered region, because the group dims once rather than per child (§6.2)
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    const std::size_t single = (8u * 64u + 8u) * 4u;
    const std::size_t overlap = (16u * 64u + 16u) * 4u;
    for (std::size_t channel = 0; channel < 4; channel++)
        EXPECT_NEAR(shot.pixels[overlap + channel], shot.pixels[single + channel], 2) << "channel " << channel;
    EXPECT_NEAR(shot.pixels[single], 230u, 2);
    EXPECT_NEAR(shot.pixels[single + 1u], 153u, 2);
}

TEST(Blend2DRenderContextChecks, ANestedLayerCompositesIntoItsParentGroup) {
    // arrange a group that holds only a second group, whose red passes through both halves (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    const Rect whole = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT});
    // act the inner group paints inside the outer one, which composites the pair onto the plate
    context.draw_rect(whole, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.push_layer(0.5f);
    context.push_layer(0.5f);
    context.draw_rect(whole, Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    context.pop_layer();
    context.pop_layer();
    surface->end_frame();
    // assert a quarter of the red reaches the plate, because each group dims the one it carries (§6.2)
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    const std::size_t centre = (32u * 64u + 32u) * 4u;
    EXPECT_NEAR(shot.pixels[centre], 242u, 2);
    EXPECT_NEAR(shot.pixels[centre + 1u], 204u, 2);
}

TEST(Blend2DRenderContextChecks, AnEmptyLayerLeavesTheTargetUntouched) {
    // arrange a white plate and a group that paints nothing (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.push_layer(0.5f);
    context.pop_layer();
    surface->end_frame();
    // assert the plate stays whole, because an empty group has nothing to composite (§6.2)
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    const std::size_t centre = (32u * 64u + 32u) * 4u;
    EXPECT_EQ(shot.pixels[centre], 255u);
    EXPECT_EQ(shot.pixels[centre + 3u], 255u);
}

TEST(Blend2DRenderContextChecks, AContextWithNoRasterKeepsTheLayerStackOnly) {
    // arrange a context over no raster, which a refused surface produces (§6.2)
    Blend2DRenderContext context(nullptr, 1.0f, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}));
    // act a mismatched pop and a real pair, which every step has to survive without pixels (§6.2)
    context.pop_layer();
    context.push_layer(0.5f);
    context.pop_layer();
    // assert the pair balanced the stack even though no raster could carry it (§6.2)
    EXPECT_FLOAT_EQ(context.opacity(), 1.0f);
    SUCCEED();
}

TEST(Blend2DRenderContextChecks, AStrokedPolylineCarriesItsComposition) {
    // arrange a white plate and a blue line under copy, which replaces the pixels it covers (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    const Point points[2] = {Point{4.0f, 24.0f}, Point{60.0f, 24.0f}};
    // act
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.draw_polyline(std::span<const Point>(points), StrokeStyle{.color = Color::rgb(0.0f, 0.0f, 1.0f), .width = 8.0f, .composition = CompositionMode::Copy});
    surface->end_frame();
    // assert the line replaced the plate with blue rather than blending over it (§6.2)
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    const std::size_t centre = (24u * 64u + 32u) * 4u;
    EXPECT_EQ(shot.pixels[centre], 0u);
    EXPECT_EQ(shot.pixels[centre + 2u], 255u);
    EXPECT_EQ(shot.pixels[centre + 3u], 255u);
}

TEST(Blend2DRenderContextChecks, AStrokedLineBatchCarriesItsComposition) {
    // arrange a white plate and a retained line batch stroked under copy, which replaces what it covers (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    ResourceFactory factory;
    const std::array<Point, 2> points{Point{4.0f, 24.0f}, Point{60.0f, 24.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(std::span<const Point>(points));
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{SCENE_WIDTH, SCENE_HEIGHT}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.draw_line_batch(batch, Transform{}, StrokeStyle{.color = Color::rgb(0.0f, 0.0f, 1.0f), .width = 8.0f, .composition = CompositionMode::Copy});
    surface->end_frame();
    // assert the stroke replaced the plate with blue rather than blending over it (§6.2)
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    const std::size_t centre = (24u * 64u + 32u) * 4u;
    EXPECT_EQ(shot.pixels[centre], 0u);
    EXPECT_EQ(shot.pixels[centre + 2u], 255u);
    EXPECT_EQ(shot.pixels[centre + 3u], 255u);
}

TEST(Blend2DRenderContextChecks, AStrokedRectangleDrawsInk) {
    // arrange a rectangle drawn with a stroke and no fill, which an outline control is (§6.2)
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_rect(Rect::from_origin_size(Point{6.0f, 6.0f}, Size{24.0f, 16.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = Color::rgb(1.0f, 1.0f, 1.0f), .stroke_width = 2.0f});
    surface->end_frame();
    // assert the stroke left ink at the outline, which is what a bordered control draws (§6.2)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    const std::size_t edge = (6u * 64u + 6u) * 4u;
    EXPECT_GT(screenshot.pixels[edge], 0u);
}

TEST(Blend2DRenderContextChecks, AnImageWithNoDestinationDrawsNothing) {
    // arrange an opaque image, which a caller may hand to an empty destination (§6.4)
    ResourceFactory factory;
    const std::vector<std::uint8_t> pixels(4 * 4 * 4, 0xff);
    const std::shared_ptr<const ImageResource> image = factory.create_image(4, 4, ImageFormat::RGBA8, pixels);
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act an image into an empty destination draws nothing, which is a refusal (§6.4)
    context.draw_image(image, Rect{}, Paint{});
    surface->end_frame();
    // assert nothing lit, because an empty destination has no area to fill (§6.4)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        if (screenshot.pixels[index] > 0u)
            ink++;
    }
    EXPECT_EQ(ink, 0u);
}

TEST(Blend2DRenderContextChecks, AnImageUnderAStrokedPaintDrawsInk) {
    // arrange an opaque image drawn with a stroke-only paint, whose alpha comes from the stroke (§6.4)
    ResourceFactory factory;
    const std::vector<std::uint8_t> pixels(4 * 4 * 4, 0xff);
    const std::shared_ptr<const ImageResource> image = factory.create_image(4, 4, ImageFormat::RGBA8, pixels);
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act
    context.draw_image(image, Rect::from_origin_size(Point{4.0f, 4.0f}, Size{4.0f, 4.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = Color::rgb(1.0f, 1.0f, 1.0f)});
    surface->end_frame();
    // assert the image reached the raster through the stroke alpha, which the modulation folds once (§6.4)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        if (screenshot.pixels[index] > 0u)
            ink++;
    }
    EXPECT_GT(ink, 0u);
}

TEST(Blend2DTextChecks, AnUnresolvedRunDrawsNothing) {
    // arrange a run whose font hash is zero, which no registered font matches (§15, AD-34)
    ShapedRun::Line line;
    line.glyphs.push_back(ShapedGlyph{.id = 1u, .position = Point{0.0f, 0.0f}});
    const TextLayout layout{.run = ShapedRun::make({std::move(line)}, 16.0f, 0u), .size = Size{20.0f, 16.0f}};
    const std::unique_ptr<Blend2DRenderSurface> surface = Blend2DRenderSurface::create(scene_metrics(1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act the run is drawn with no resolvable font, which must refuse rather than crash (§9.2)
    context.draw_text(layout, Point{8.0f, 16.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    surface->end_frame();
    // assert nothing lit, because an unresolvable run draws nothing (§9.2, AD-34)
    const Screenshot screenshot = capture_surface(*surface);
    ASSERT_FALSE(screenshot.empty());
    std::size_t ink = 0;
    for (std::size_t index = 0; index < screenshot.pixels.size(); index += 4) {
        if (screenshot.pixels[index] > 0u)
            ink++;
    }
    EXPECT_EQ(ink, 0u);
}
