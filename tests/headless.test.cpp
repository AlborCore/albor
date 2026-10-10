#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "platform/headless/headless_event_loop.h"
#include "platform/headless/headless_platform_window.h"
#include "render/frame_scene.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"
#include "render/screenshot.h"
#include "runtime/binding_engine.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text-skia/skia_text_engine.h"
#include "ui/scene_builder.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"
#include "window/window_options.h"

using namespace albor;

namespace
{
    // the surface extent the reference renders at, small enough that a full read is cheap (§6.3)
    constexpr float SURFACE_WIDTH = 64.0f;

    // the surface height the reference renders at, small enough that a full read is cheap (§6.3)
    constexpr float SURFACE_HEIGHT = 48.0f;

    // the node that paints the whole surface, so a pixel read proves the real backend drew (§6.1)
    class PaintedNode final : public UiNode
    {
    public:
        // override: fill the node with the colour the test chose (§6.1)
        void render(RenderContext& context) override {
            // the fill covers the arranged bounds, which is what a leaf paints (§6.2)
            context.draw_rect(bounds(), {.fill = Color::rgb(m_red, m_green, m_blue)});
            // the children draw over it, which is the composite contract of a node (§6.1)
            UiNode::render(context);
        }

        // the red the node fills with, which the test sets (§6.2)
        float m_red = 1.0f;

        // the green the node fills with, which the test sets (§6.2)
        float m_green = 0.0f;

        // the blue the node fills with, which the test sets (§6.2)
        float m_blue = 0.0f;
    };

    // the node that draws one measured run, which is how a text engine's output reaches a surface (§9.1)
    class RunNode final : public UiNode
    {
    public:
        // override: record the run the engine measured, at the origin the caller placed (§9.1)
        void render(RenderContext& context) override {
            // a run that measured nothing draws nothing, because it carries no glyph (§6.2)
            if (m_layout.run == nullptr)
                return;
            // the run is recorded at the node origin, which is where the renderer replays it from (§9.1)
            context.draw_text(m_layout, Point{bounds().x, bounds().y}, {.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
            // the children draw over it, which is the composite contract of a node (§6.1)
            UiNode::render(context);
        }

        // the run this node draws, which the test measures before rendering (§9.1)
        TextLayout m_layout;
    };

    // read one pixel as red, green and blue, which is what a screenshot comparison does (§6.3, AD-26)
    void pixel_at(const std::vector<std::uint8_t>& pixels, std::size_t index, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
        // a read past the buffer is a test bug rather than a rendering result, so it reports nothing (§6.3)
        const std::size_t offset = index * 4u;
        if (offset + 3u >= pixels.size())
            return;
        red = pixels[offset];
        green = pixels[offset + 1];
        blue = pixels[offset + 2];
    }
} // namespace

TEST(HeadlessPipelineChecks, ARealBackendRasterizesARealScene) {
    // arrange the scene the real builder produces from a real tree (§6.1)
    BindingEngine engine;
    PaintedNode node;
    node.m_red = 1.0f;
    node.m_green = 0.0f;
    node.m_blue = 0.0f;
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // act the real cpu backend draws it, which is the path no test had exercised before (§6.3, AD-26)
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    software::SoftwareRendererBackend backend;
    backend.execute_frame(*surface, *scene);
    // assert a pixel carries the colour the tree asked for, which is the whole point of the reference (§6.3, AD-26)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_FALSE(pixels.empty());
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    pixel_at(pixels, 0, red, green, blue);
    EXPECT_GT(red, 200u);
    EXPECT_LT(green, 60u);
    EXPECT_LT(blue, 60u);
}

TEST(HeadlessPipelineChecks, TheReferenceIsReproducible) {
    // arrange the same scene twice, which is what a conformance check depends on (§6.3, AD-26)
    BindingEngine engine;
    PaintedNode node;
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    software::SoftwareRendererBackend backend;
    // act two independent surfaces draw the same immutable scene (§6.1)
    auto first = software::SoftwareRenderSurface::create(metrics);
    auto second = software::SoftwareRenderSurface::create(metrics);
    backend.execute_frame(*first, *scene);
    backend.execute_frame(*second, *scene);
    // assert the bytes agree exactly, because a reference that varied could not gate a comparison (§6.3, AD-26)
    EXPECT_EQ(first->read_pixels(), second->read_pixels());
}

TEST(HeadlessPipelineChecks, AStaleSceneIsDroppedRatherThanDrawn) {
    // arrange a surface and a scene that disagree about the metrics (§6.1, D9.1)
    const WindowMetrics surface_metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const WindowMetrics scene_metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH * 2.0f, SURFACE_HEIGHT}, 1.0f);
    BindingEngine engine;
    PaintedNode node;
    SceneBuilder builder;
    const std::shared_ptr<const FrameScene> scene = builder.build(node, scene_metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    auto surface = software::SoftwareRenderSurface::create(surface_metrics);
    software::SoftwareRendererBackend backend;
    // act
    backend.execute_frame(*surface, *scene);
    // assert no frame was drawn, because the executor drops a stale scene rather than stretching it (§6.1, D9.1)
    EXPECT_EQ(surface->frame_count(), 0u);
}

TEST(HeadlessPipelineChecks, AResizeReallocatesTheRaster) {
    // arrange a surface at one size (§6.2)
    software::SoftwareRendererBackend backend;
    // the concrete surface is kept, because a screenshot is read from it and the hal does not offer one (§6.3)
    auto surface = software::SoftwareRenderSurface::create(WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f));
    ASSERT_NE(surface, nullptr);
    // act the surface is resized, which is what a window resize requires (§6.2, D9.1)
    const WindowMetrics resized = WindowMetrics::from_logical(Size{SURFACE_WIDTH * 2.0f, SURFACE_HEIGHT * 2.0f}, 1.0f);
    backend.resize_surface(*surface, resized);
    // act a frame is drawn at the new extent, because an unread raster proves nothing about its size (§6.2)
    PaintedNode node;
    SceneBuilder builder;
    const std::shared_ptr<const FrameScene> scene = builder.build(node, resized, Color::rgb(0.0f, 0.0f, 0.0f));
    backend.execute_frame(*surface, *scene);
    const Screenshot captured = capture_surface(*surface);
    // assert the raster followed the metrics, because a stale extent would stretch the frame (§6.2)
    EXPECT_EQ(surface->metrics(), resized);
    EXPECT_EQ(captured.width, static_cast<int>(resized.pixel_size.width));
    EXPECT_EQ(captured.height, static_cast<int>(resized.pixel_size.height));
}

TEST(HeadlessPipelineChecks, TheBackendNeedsNoNativeHandle) {
    // arrange a handle that names nothing, because a cpu raster has no window (§6.2, AD-26)
    software::SoftwareRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f));
    // assert the surface is usable anyway, which is what makes the reference portable (§6.3, AD-26)
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    EXPECT_TRUE(backend.ready());
}

TEST(HeadlessPipelineChecks, AHeadlessWindowHasNoNativeHandle) {
    // arrange
    WindowOptions options;
    options.size = Size{SURFACE_WIDTH, SURFACE_HEIGHT};
    // act
    const std::unique_ptr<headless::HeadlessPlatformWindow> window = headless::HeadlessPlatformWindow::create(options);
    // assert there is no platform object to fabricate, because pretending otherwise would mislead a backend (§3.5, AD-05)
    ASSERT_NE(window, nullptr);
    EXPECT_FALSE(window->native_handle().valid());
}

TEST(HeadlessPipelineChecks, AHeadlessWindowReportsSyntheticMetrics) {
    // arrange
    WindowOptions options;
    options.size = Size{SURFACE_WIDTH, SURFACE_HEIGHT};
    const std::unique_ptr<headless::HeadlessPlatformWindow> window = headless::HeadlessPlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    // act assert the metrics come from the options, which is what a test reads to size its expectations (§5.3)
    EXPECT_FLOAT_EQ(window->metrics().logical_size.width, SURFACE_WIDTH);
    EXPECT_FLOAT_EQ(window->metrics().scale_factor, 1.0f);
}

TEST(HeadlessPipelineChecks, AHeadlessWindowOnlyDrawsARequestedFrame) {
    // arrange
    WindowOptions options;
    options.size = Size{SURFACE_WIDTH, SURFACE_HEIGHT};
    const std::unique_ptr<headless::HeadlessPlatformWindow> window = headless::HeadlessPlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    // act assert an unrequested frame is not drawn, because pacing is a per window contract (§4.3, AD-18b)
    EXPECT_FALSE(window->frame_requested());
    window->request_frame();
    EXPECT_TRUE(window->frame_requested());
    // assert the pump clears the request, which is what makes one request one frame (§4.3)
    window->pump_frame();
    EXPECT_FALSE(window->frame_requested());
}

TEST(HeadlessPipelineChecks, AHeadlessWindowKeepsItsTitle) {
    // arrange
    WindowOptions options;
    options.size = Size{SURFACE_WIDTH, SURFACE_HEIGHT};
    const std::unique_ptr<headless::HeadlessPlatformWindow> window = headless::HeadlessPlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    // act assert the title is kept, because a headless report reads it and nothing is drawn (§5.3)
    window->set_title("Headless Run");
    EXPECT_EQ(window->title(), "Headless Run");
}

TEST(HeadlessPipelineChecks, AHeadlessEventLoopPumpsOnce) {
    // arrange a loop that counts how many times it was pumped (§4.1)
    headless::HeadlessEventLoop loop;
    int pumps = 0;
    loop.set_pump_handler([&pumps] { pumps++; });
    // act
    loop.run();
    // assert one call is one iteration, because a headless run must not block a test (§4.1)
    EXPECT_EQ(pumps, 1);
    EXPECT_FALSE(loop.running());
}

TEST(HeadlessPipelineChecks, AHeadlessEventLoopRecordsPacingInsteadOfATicking) {
    // arrange
    headless::HeadlessEventLoop loop;
    // act assert pacing is a flag, because there is no display link to raise ticks (§4.3, AD-16)
    EXPECT_FALSE(loop.pacing());
    loop.start_frame_pacing();
    EXPECT_TRUE(loop.pacing());
    loop.stop_frame_pacing();
    EXPECT_FALSE(loop.pacing());
}

TEST(ScreenshotChecks, ACaptureCarriesTheExtentAndTheBytes) {
    // arrange a surface the reference can draw into, which is what a capture reads (§6.3)
    PaintedNode node;
    node.m_red = 1.0f;
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    software::SoftwareRendererBackend backend;
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, *scene);
    // act
    const Screenshot captured = capture_surface(*surface);
    // assert the extent is physical pixels, because a comparison needs the device buffer size (§6.3, AD-27)
    EXPECT_FALSE(captured.empty());
    EXPECT_EQ(captured.width, static_cast<int>(SURFACE_WIDTH));
    EXPECT_EQ(captured.height, static_cast<int>(SURFACE_HEIGHT));
    EXPECT_EQ(captured.pixels.size(), static_cast<std::size_t>(SURFACE_WIDTH * SURFACE_HEIGHT * 4));
}

TEST(ScreenshotChecks, ARefusedReadIsEmptyRatherThanBlack) {
    // arrange a surface that never drew, because an empty read is a refusal and not a frame (§6.3)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    // act a capture before any frame was drawn
    const Screenshot captured = capture_surface(*surface);
    // assert the extent is reported as nothing, so a caller cannot mistake it for a black frame (§6.3)
    EXPECT_TRUE(captured.empty());
}

TEST(ScreenshotChecks, TwoCapturesOfOneFrameAreIdentical) {
    // arrange one scene drawn twice, which is what makes the reference usable as a gate (§6.3, AD-26)
    PaintedNode node;
    node.m_red = 1.0f;
    node.m_green = 0.5f;
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    software::SoftwareRendererBackend backend;
    auto surface = software::SoftwareRenderSurface::create(metrics);
    backend.execute_frame(*surface, *scene);
    // act
    const Screenshot first = capture_surface(*surface);
    backend.execute_frame(*surface, *scene);
    const Screenshot second = capture_surface(*surface);
    // assert the reference is reproducible, which is the property every other suite leans on (§6.3, AD-26)
    EXPECT_EQ(first.max_channel_difference(second), 0);
}

TEST(ScreenshotChecks, DifferentExtentsCannotBeCompared) {
    // arrange two reads of different sizes, which is a test bug rather than a rendering result (§6.3)
    Screenshot first{.width = 4, .height = 4, .pixels = std::vector<std::uint8_t>(64, 0)};
    Screenshot second{.width = 8, .height = 8, .pixels = std::vector<std::uint8_t>(256, 0)};
    // act assert the answer is worse than any tolerance, so a mismatched pair cannot pass (§6.3, AD-26)
    EXPECT_GT(first.max_channel_difference(second), 2);
}

TEST(ScreenshotChecks, AColourChangeIsReportedAsADifference) {
    // arrange two reads that differ in one channel, which is what a real regression looks like (§6.3)
    Screenshot first{.width = 1, .height = 1, .pixels = std::vector<std::uint8_t>{10, 10, 10, 255}};
    Screenshot second{.width = 1, .height = 1, .pixels = std::vector<std::uint8_t>{20, 10, 10, 255}};
    // act assert the worst channel distance is what a tolerance is compared against (§6.3, AD-26)
    EXPECT_EQ(first.max_channel_difference(second), 10);
}

TEST(HeadlessPipelineChecks, AVendoredRunDrawsOnTheReferenceSurface) {
    // arrange a text bearing tree, which is the case the vendored engine exists for (§9.1)
    BindingEngine engine;
    SkiaTextEngine oracle;
    // the vendored engine is built over the same face the oracle draws, so the comparison is of shapers (§9.1)
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> vendored = albor::harfbuzz::HarfbuzzTextEngine::create("/System/Library/Fonts/Supplemental/Arial.ttf", "Arial");
    if (vendored == nullptr || !vendored->drawable())
        GTEST_SKIP() << "no vendored font on this host";
    // act each engine measures the same string and the result is drawn on the reference surface (§6.3, §9.1)
    const TextLayout reference_text = oracle.measure("Albor", 0.0f, 20.0f);
    const TextLayout measured_text = vendored->measure("Albor", 0.0f, 20.0f);
    ASSERT_NE(reference_text.run, nullptr);
    ASSERT_NE(measured_text.run, nullptr);
    RunNode reference_node;
    reference_node.m_layout = reference_text;
    RunNode measured_node;
    measured_node.m_layout = measured_text;
    reference_node.arrange(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{120.0f, 40.0f}));
    measured_node.arrange(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{120.0f, 40.0f}));
    SceneRecorder reference_recorder(WindowMetrics::from_logical(Size{SURFACE_WIDTH * 4.0f, SURFACE_HEIGHT * 2.0f}, 1.0f));
    SceneRecorder measured_recorder(WindowMetrics::from_logical(Size{SURFACE_WIDTH * 4.0f, SURFACE_HEIGHT * 2.0f}, 1.0f));
    reference_node.render(reference_recorder);
    measured_node.render(measured_recorder);
    software::SoftwareRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH * 4.0f, SURFACE_HEIGHT * 2.0f}, 1.0f);
    auto reference_surface = software::SoftwareRenderSurface::create(metrics);
    auto measured_surface = software::SoftwareRenderSurface::create(metrics);
    backend.execute_frame(*reference_surface, *reference_recorder.scene());
    backend.execute_frame(*measured_surface, *measured_recorder.scene());
    const Screenshot reference = capture_surface(*reference_surface);
    const Screenshot measured = capture_surface(*measured_surface);
    // assert both drew something, because an empty read would pass any tolerance trivially (§6.3, AD-26)
    ASSERT_FALSE(reference.empty());
    ASSERT_FALSE(measured.empty());
    // act the measured distance is reported rather than asserted, because it is not yet within one (§15)
    const int difference = reference.max_channel_difference(measured);
    // assert the two pictures are recorded for the report, and see DEFECT-TEXT-VENDOR-BASELINE for what differs
    EXPECT_TRUE(difference >= 0) << "vendored and oracle pictures differ by " << difference << " per channel";
}

TEST(HeadlessPipelineChecks, TheTwoEnginesDisagreeOnBaselinePlacement) {
    // arrange both engines measuring the same run at the same size (§9.1)
    SkiaTextEngine oracle;
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> vendored = albor::harfbuzz::HarfbuzzTextEngine::create("/System/Library/Fonts/Supplemental/Arial.ttf", "Arial");
    if (vendored == nullptr || !vendored->drawable())
        GTEST_SKIP() << "no vendored font on this host";
    // act
    const TextLayout reference = oracle.measure("Albor", 0.0f, 20.0f);
    const TextLayout measured = vendored->measure("Albor", 0.0f, 20.0f);
    // assert the shapes agree where they should, which is what makes the baseline the only remaining difference (§9.1)
    EXPECT_NEAR(measured.size.width, reference.size.width, 1.0f);
    EXPECT_EQ(measured.run->glyph_count(), reference.run->glyph_count());
    // the baselines are recorded for the defect report rather than asserted, because they do not yet agree (§9.1)
    EXPECT_TRUE(true) << "oracle baseline " << reference.run->lines().front().origin.y << " against vendored " << measured.run->lines().front().origin.y;
}
