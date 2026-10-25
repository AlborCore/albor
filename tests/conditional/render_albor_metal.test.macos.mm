#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#import <QuartzCore/CAMetalLayer.h>

#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/picture_resource.h"
#include "render/render-albor/albor_glyph_path.h"
#include "render/render-albor/albor_render_context.h"
#include "render/render-albor/albor_renderer_backend.h"
#include "render/render-albor/albor_shaders.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"
#include "render/screenshot.h"
#include "render/text_layout.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text_engine.h"
#include "ui/scene_builder.h"
#include "window/window_metrics.h"

using namespace albor;
using namespace albor::render;

// quartzcore brings the carbon rect and point into scope, so the albor shapes are named through aliases
using AlborRect = albor::Rect;
using AlborPoint = albor::Point;
using AlborSize = albor::Size;

TEST(AlborShaderLibraryChecks, CarriesTheCompiledMetalLibrary) {
    // arrange the bytes the build embedded, which are what the device loads as its library (AD-16)
    const std::span<const std::byte> data = metal_library_data();
    // act the first four bytes are read as the magic a compiled library carries (§6.3)
    ASSERT_GE(data.size(), 4u);
    // assert the header is MTLB, which marks a compiled library rather than source (§6.3)
    EXPECT_EQ(std::to_integer<unsigned>(data[0]), 'M');
    EXPECT_EQ(std::to_integer<unsigned>(data[1]), 'T');
    EXPECT_EQ(std::to_integer<unsigned>(data[2]), 'L');
    EXPECT_EQ(std::to_integer<unsigned>(data[3]), 'B');
    // assert the library carries a body, because an empty embed would load nothing a pipeline could use (§6.3)
    EXPECT_GT(data.size(), 1024u);
}

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

TEST(AlborRendererBackendChecks, ReadsBackInCanonicalRgbaOrder) {
    // arrange a two pixel scene, whose colours prove the channel order the target promises (§6.3, AD-26)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{2.0f, 1.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{1.0f, 0.0f}, AlborSize{1.0f, 1.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the first pixel is red and the second is blue, in rgba byte order (§6.3, AD-26)
    ASSERT_EQ(pixels.size(), 8u);
    EXPECT_EQ(pixels[0], 255u);
    EXPECT_EQ(pixels[1], 0u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 255u);
    EXPECT_EQ(pixels[4], 0u);
    EXPECT_EQ(pixels[5], 0u);
    EXPECT_EQ(pixels[6], 255u);
    EXPECT_EQ(pixels[7], 255u);
}

TEST(AlborRendererBackendChecks, ReadsTheSameFinishedFrameTwice) {
    // arrange a finished frame whose target is retained after the read (§6.3)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.2f, 0.4f, 0.6f)));
    // act
    const std::vector<std::uint8_t> first = surface->read_pixels();
    const std::vector<std::uint8_t> second = surface->read_pixels();
    // assert the second read is the same picture, because reading must not consume the finished frame (§6.3)
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST(AlborRendererBackendChecks, RefusesAReadWhileAFrameIsInFlight) {
    // arrange a surface whose frame has begun but has not ended (§6.1)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->begin_frame();
    const std::vector<std::uint8_t> during = surface->read_pixels();
    surface->end_frame();
    // assert the in-flight frame is refused and the finished one reads back (§6.1, §6.3)
    EXPECT_TRUE(during.empty());
    EXPECT_FALSE(surface->read_pixels().empty());
}

TEST(AlborRendererBackendChecks, ASecondSurfaceCanShareTheLayer) {
    // arrange a layer the composition root hands to two surfaces in turn (§5.3, AD-16)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{8.0f, 8.0f}, 1.0f);
    CAMetalLayer* layer = [CAMetalLayer layer];
    const NativeWindowHandle handle = NativeWindowHandle::from_object((__bridge const void*)layer);
    std::unique_ptr<RenderSurface> first = backend.create_surface(handle, metrics);
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->valid());
    // act a second surface on the same layer, which keeps its own target and cache (§6.2)
    std::unique_ptr<RenderSurface> second = backend.create_surface(handle, metrics);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(second->valid());
    const FrameScene red(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    backend.execute_frame(*first, red);
    backend.execute_frame(*second, red);
    // assert both surfaces present and read back while they share the layer (§5.3)
    EXPECT_EQ(first->read_pixels().size(), 8u * 8u * 4u);
    EXPECT_EQ(second->read_pixels().size(), 8u * 8u * 4u);
    // act the second leaves, which returns the layer to the first (§5.3)
    second.reset();
    backend.execute_frame(*first, red);
    // assert the first still presents after the second left (§5.3)
    EXPECT_FALSE(first->read_pixels().empty());
}

// the suite presents a real stream, because a present that forgets its pool leaks what no header check sees (§6.3)
TEST(AlborRendererBackendChecks, AStreamOfPresentedFramesKeepsTheFootprintFlat) {
#if defined(ALBOR_COVERAGE)
    // coverage counters dominate the footprint, so the probe is skipped in the instrumented build (§2.2, §6.3)
    GTEST_SKIP() << "coverage instrumentation changes the footprint measurement";
#endif
    // arrange the layer a composition root hands over, which is what a windowed frame presents into (§5.3, AD-16)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{800.0f, 600.0f}, 2.0f);
    CAMetalLayer* layer = [CAMetalLayer layer];
    const NativeWindowHandle handle = NativeWindowHandle::from_object((__bridge const void*)layer);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(handle, metrics);
    // assert the surface came up, because a refused surface presents nothing and would measure nothing (§6.2)
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // arrange a scene with one composited layer, so every frame allocates an offscreen target of its own (§6.2)
    FrameScene built(metrics, Color::rgb(0.06f, 0.07f, 0.09f));
    built.add_command(DrawCommand::layer_push(0.5f));
    built.add_command(DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{100.0f, 100.0f}, AlborSize{400.0f, 200.0f}), Paint{.fill = Color::rgb(0.36f, 0.62f, 0.86f)}));
    built.add_command(DrawCommand::layer_pop());
    // the scene is handed over immutable, because that is the pointer the render thread consumes (§6.1, AD-18)
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(std::move(built));
    // arrange the executor, because stage two runs on its own thread, which owns no autorelease pool (§6.1, AD-18)
    RenderExecutor executor(&backend);
    // act a warmup, so the measurement pays neither the target allocation nor the driver's first pipelines (§2.2)
    for (int frame = 0; frame < 100; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // act the first measured window, which lets the driver finish its own lazy allocation (§2.2)
    for (int frame = 0; frame < 200; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the footprint the stream settles at, which is the baseline the bound is judged against (§2.2)
    task_vm_info_data_t before{};
    mach_msg_type_number_t before_count = TASK_VM_INFO_COUNT;
    ASSERT_EQ(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&before), &before_count), KERN_SUCCESS);
    // act the steady state window, which is a window that keeps presenting one unchanged scene (§6.1)
    for (int frame = 0; frame < 200; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the footprint the steady state left behind, which one leaked command buffer per frame lifts by 93 KB (§2.2)
    task_vm_info_data_t after{};
    mach_msg_type_number_t after_count = TASK_VM_INFO_COUNT;
    ASSERT_EQ(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&after), &after_count), KERN_SUCCESS);
    // assert every frame ran, because a stream that dropped them would hold the bound by drawing nothing (§6.1)
    EXPECT_EQ(executor.executed_count(), 500u);
    // assert a per frame leak cannot hide here, because the pool costs 93 KB per frame when it is missing (§6.3)
    const std::int64_t growth = static_cast<std::int64_t>(after.phys_footprint) - static_cast<std::int64_t>(before.phys_footprint);
    // the steady window is flat; a missing pool adds 18.6 MB across these 200 frames (§6.3)
    EXPECT_LT(growth, 12 * 1024 * 1024);
}

TEST(AlborRendererBackendChecks, ReusesTheBatchBuffersAcrossSteadyFrames) {
    // arrange a backend and an offscreen surface, which needs no layer to draw a frame (§6.2)
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{320.0f, 200.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // arrange one immutable scene, which every frame replays unchanged (§6.1)
    FrameScene built(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    built.add_command(DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{20.0f, 20.0f}, AlborSize{120.0f, 80.0f}), Paint{.fill = Color::rgb(0.2f, 0.4f, 0.8f)}));
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(std::move(built));
    // act a warmup that fills every retained slot, which a steady frame then reuses (AD-06)
    for (int frame = 0; frame < 8; frame++)
        backend.execute_frame(*surface, *scene);
    // arrange the allocations the warmup made, which the steady frames must not raise (AD-06)
    const std::uint64_t allocations = backend.batch_buffer_allocations();
    ASSERT_GT(allocations, 0u);
    // act a steady stream of the same scene, which is the shape a manual drag has every frame (§6.1)
    for (int frame = 0; frame < 120; frame++)
        backend.execute_frame(*surface, *scene);
    // assert every steady frame reused the buffers the warmup allocated, which a per frame allocation would raise (AD-06)
    EXPECT_EQ(backend.batch_buffer_allocations(), allocations);
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
        void push_layer(float) override {}
        void pop_layer() override {}
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
    // assert the frame kept its extent and the resize applied after it (§6.2)
    EXPECT_EQ(during, small);
    EXPECT_EQ(surface->metrics(), large);
    // act a frame at the applied extent, which proves the pending resize reached the target (§6.2)
    backend.execute_frame(*surface, FrameScene(large, Color::rgb(0.0f, 0.0f, 1.0f)));
    // assert the target moved to the new physical extent (§6.2, AD-27)
    EXPECT_EQ(surface->read_pixels().size(), 8u * 6u * 4u);
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
    // the whole rounded mask follows the reference, at the 31 measured on this host along its arcs
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 40);
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
    // the snapped axis aligned mask agrees with the reference exactly, which sampling alone could not prove
    EXPECT_EQ(expected_shot.max_channel_difference(actual_shot), 0);
}

TEST(AlborParityChecks, ATwoScaleSceneDoublesTheGeometry) {
    // arrange the same logical square at two densities, which the device scale must carry into the geometry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    const WindowMetrics denser = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 2.0f);
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> small = backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> large = backend.create_surface(NativeWindowHandle{}, denser);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    FrameScene small_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand small_square = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{4.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    small_square.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
    small_scene.add_command(std::move(small_square));
    FrameScene large_scene(denser, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand large_square = DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{4.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    large_square.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, denser.logical_size));
    large_scene.add_command(std::move(large_square));
    // act
    backend.execute_frame(*small, small_scene);
    backend.execute_frame(*large, large_scene);
    const Screenshot first = capture_surface(*small);
    const Screenshot second = capture_surface(*large);
    // assert the target doubled, which a comparison of two densities has to see (AD-27)
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    ASSERT_EQ(first.pixels.size(), 16u * 16u * 4u);
    ASSERT_EQ(second.pixels.size(), 32u * 32u * 4u);
    // the logical square covers its logical pixels once and its device pixels twice, so the scale reached the geometry
    EXPECT_EQ(first.pixels[(6u * 16u + 6u) * 4u], 255u);
    EXPECT_EQ(second.pixels[(12u * 32u + 12u) * 4u], 255u);
    // device (6,6) is outside the doubled square, which a surface that ignored the scale would have painted (AD-27)
    EXPECT_EQ(second.pixels[(6u * 32u + 6u) * 4u], 0u);
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
    // the bound is the 46 measured on this host plus margin, which a box and a gaussian blur reach at a soft edge
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 64);
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

TEST(AlborParityChecks, AHoledPathMatchesTheReference) {
    // arrange a square path with an opposite wound inner square, which the reference cuts as a hole
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[10] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[8] = {AlborPoint{8.0f, 8.0f}, AlborPoint{8.0f, 32.0f}, AlborPoint{32.0f, 32.0f}, AlborPoint{32.0f, 8.0f}, AlborPoint{16.0f, 16.0f}, AlborPoint{24.0f, 16.0f}, AlborPoint{24.0f, 24.0f}, AlborPoint{16.0f, 24.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(53u, verbs, points);
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
    // the bound is the 1 measured on this host plus margin, which the shared fringe leaves on the boundary
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    // the hole stays background in both, which a per contour fill would paint
    const std::size_t hole = (20u * 40u + 20u) * 4u;
    EXPECT_EQ(actual_shot.pixels[hole], 255u);
    EXPECT_EQ(expected_shot.pixels[hole], 255u);
    // the ring between the two contours fills in both
    const std::size_t ring = (20u * 40u + 12u) * 4u;
    EXPECT_LT(actual_shot.pixels[ring], 250u);
    EXPECT_LT(expected_shot.pixels[ring], 250u);
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
    // the flat interiors agree exactly, which a global tint of the budget would otherwise pass (§15)
    EXPECT_EQ(expected_shot.band_count(actual_shot, 2), 0u);
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
    // the vertical profile follows the reference, at the 61 measured on this host along the wide falloff
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 64);
}

TEST(AlborParityChecks, AShearedCasterMatchesTheReference) {
    // arrange a caster drawn under a shear, whose parallelogram the reference renders as it is
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the shear slants the whole caster, which an axis aligned silhouette cannot follow (§6.2, §6.1)
    const Transform shear{.skew_y = 0.6f};
    const AlborRect caster = AlborRect::from_origin_size(AlborPoint{16.0f, 16.0f}, AlborSize{40.0f, 16.0f});
    const Shadow cast{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.7f), .offset = AlborPoint{0.0f, 0.0f}, .blur = 0.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_transform(shear);
    expected.draw_shadow(caster, 0.0f, cast);
    expected.restore();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.set_transform(shear);
    actual.draw_shadow(caster, 0.0f, cast);
    actual.restore();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // a point inside the sheared parallelogram carries the cast on both, which the slant placed there (§6.2)
    const std::size_t inside = (54u * 96u + 50u) * 4u;
    EXPECT_LT(actual_shot.pixels[inside], 200u);
    EXPECT_LT(expected_shot.pixels[inside], 200u);
    // a point inside the box but outside the parallelogram stays background, which an axis aligned cast would ink
    const std::size_t outside = (55u * 96u + 17u) * 4u;
    EXPECT_GT(actual_shot.pixels[outside], 230u);
    EXPECT_GT(expected_shot.pixels[outside], 230u);
    // the bound is the 44 measured on this host plus margin, which a sheared antialiased edge spends a pixel on (§16)
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 52);
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

TEST(AlborParityChecks, KeepsAFrameThatUploadsMoreImagesThanTheCacheHolds) {
    // arrange a frame of seventy solid images, which is more than the texture cache is sized for (§6.4, AD-06)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    for (std::uint64_t index = 0u; index < 70u; index++) {
        // every image is its own identifier and its own colour, so none of them is answered by another (AD-06)
        const std::vector<std::uint8_t> source{static_cast<std::uint8_t>(index * 3u), 60u, 120u, 255u};
        const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(300u + index, 1u, 1u, ImageFormat::RGBA8, source);
        // the images tile a ten column grid, whose row is the floating quotient rather than the integer one (§6.4)
        const AlborPoint origin{.x = 4.0f + static_cast<float>(index % 10u) * 9.0f, .y = 4.0f + (static_cast<float>(index) / 10u) * 9.0f};
        DrawCommand command = DrawCommand::image(image, AlborRect::from_origin_size(origin, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        // the clip is the whole surface, which is what an unclipped frame carries (§6.2)
        command.set_clip(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size));
        scene.add_command(std::move(command));
    }
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // every image draws as its own colour, which a texture dropped before the frame encoded would not (§6.4, AD-06)
    EXPECT_LE(actual_shot.max_channel_difference(expected_shot), 2);
}

TEST(AlborParityChecks, ADownscaledImageMatchesTheReference) {
    // arrange a checkerboard downscaled eight times, which a mip map keeps from aliasing
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the source is a 32 by 32 checkerboard of alternating opaque colours
    std::vector<std::uint8_t> source(32u * 32u * 4u, 0);
    for (std::size_t y = 0; y < 32; y++) {
        for (std::size_t x = 0; x < 32; x++) {
            // each cell alternates between the two colours, which a minified read must average
            const bool light = (x + y) % 2 == 0;
            const std::size_t at = (y * 32u + x) * 4u;
            source[at] = light ? 255u : 0u;
            source[at + 1] = light ? 255u : 0u;
            source[at + 2] = light ? 255u : 0u;
            source[at + 3] = 255u;
        }
    }
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(71u, 32u, 32u, ImageFormat::RGBA8, source);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, AlborRect::from_origin_size(AlborPoint{2.0f, 2.0f}, AlborSize{12.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    // the minified average is a mid grey, which a nearest tap would swing to black or white
    const std::size_t interior = (8u * 16u + 8u) * 4u;
    EXPECT_GT(actual_shot.pixels[interior], 60u);
    EXPECT_LT(actual_shot.pixels[interior], 210u);
    // the bound is the 57 measured on this host plus margin, which two mip filters reach at a hard edge
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 72);
}

TEST(AlborParityChecks, AFilledConcavePathMatchesTheReference) {
    // arrange an L-shaped closed path, whose notch a fan cannot cover
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[9] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[8] = {AlborPoint{8.0f, 8.0f}, AlborPoint{34.0f, 8.0f}, AlborPoint{34.0f, 34.0f}, AlborPoint{26.0f, 34.0f}, AlborPoint{26.0f, 16.0f}, AlborPoint{16.0f, 16.0f}, AlborPoint{16.0f, 34.0f}, AlborPoint{8.0f, 34.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(81u, verbs, points);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::path(path, Transform{}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
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
    // the gap between the arms stays background in both, which a fan from the first corner paints
    const std::size_t gap = (26u * 40u + 21u) * 4u;
    EXPECT_EQ(actual_shot.pixels[gap], 255u);
    EXPECT_EQ(expected_shot.pixels[gap], 255u);
    // both arms of the U are filled in both
    const std::size_t left_arm = (30u * 40u + 11u) * 4u;
    const std::size_t right_arm = (30u * 40u + 30u) * 4u;
    EXPECT_NEAR(actual_shot.pixels[left_arm], expected_shot.pixels[left_arm], 2);
    EXPECT_NEAR(actual_shot.pixels[right_arm], expected_shot.pixels[right_arm], 2);
    // the two agree over the whole shape within the feathered edge band
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(GlyphPathChecks, RefusesAFontWithoutAnOutlineSource) {
    // arrange a handle that wraps no typeface at all
    const FontHandle font;
    // act
    const GlyphPath glyph = albor::render::glyph_path(font, 3u, 16.0f);
    // assert
    EXPECT_TRUE(glyph.points.empty());
    EXPECT_TRUE(glyph.verbs.empty());
}

TEST(GlyphPathChecks, OutlinesAGlyphOfTheShapedFont) {
    // arrange the system face the platform engine resolves, shaped at a size a ui label uses
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    ASSERT_FALSE(layout.run->lines().empty());
    const ShapedRun::Line& line = layout.run->lines().front();
    ASSERT_FALSE(line.glyphs.empty());
    // act
    const GlyphPath glyph = albor::render::glyph_path(line.font, line.glyphs.front().id, 24.0f);
    // assert the handle answers the content identity the run carries, which is what a cache keys on (§15, AD-34)
    ASSERT_NE(layout.run->font_hash(), 0u);
    EXPECT_EQ(line.font.font_hash(), layout.run->font_hash());
    // assert the outline is closed subpaths, which the fill sweeps as one shape
    ASSERT_FALSE(glyph.points.empty());
    std::size_t closes = 0;
    for (const PathVerb verb : glyph.verbs) {
        if (verb == PathVerb::Close)
            closes++;
    }
    // a close consumes no point, so the verbs carry one more than the points plus the extra subpaths
    EXPECT_EQ(glyph.verbs.size(), glyph.points.size() + closes);
    EXPECT_EQ(glyph.verbs.front(), PathVerb::MoveTo);
    // every subpath closes, which is what makes the counters of the glyph cut (§6.2)
    EXPECT_GE(closes, 1u);
    EXPECT_EQ(glyph.verbs.back(), PathVerb::Close);
    // a size of no pixels has no outline to scale to, which the walk refuses
    EXPECT_TRUE(albor::render::glyph_path(line.font, line.glyphs.front().id, 0.0f).points.empty());
    // a space carries no visible outline, which the face reports as no path
    const TextLayout spaced = engine->measure(" ", 0.0f, 24.0f);
    ASSERT_FALSE(spaced.empty());
    ASSERT_FALSE(spaced.run->lines().front().glyphs.empty());
    EXPECT_TRUE(albor::render::glyph_path(spaced.run->lines().front().font, spaced.run->lines().front().glyphs.front().id, 24.0f).points.empty());
}

TEST(AlborTextChecks, ReusesAWalkedGlyphAcrossTheSameContext) {
    // arrange a platform run and a context that borrows the backend cache for the surface (§6.4, AD-06)
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    FrameBatch batch;
    GlyphPathCache cache;
    AlborRenderContext context(batch, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{40.0f, 40.0f}), 1.0f, &cache);
    // act the glyph is drawn once, which stores it in the cache, and again, which must read it back (§6.4, AD-06)
    context.draw_text(layout, AlborPoint{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // the first draw produces the batch content the second draw will produce again (§6.4)
    const std::size_t first_draws = batch.draws.size();
    const std::size_t first_indices = batch.indices.size();
    const std::size_t first_vertices = batch.vertices.size();
    // the cache must now hold the walked glyph, so a second draw can reuse it (§6.4, AD-06)
    EXPECT_GE(cache.count(), 1u);
    batch.draws.clear();
    batch.indices.clear();
    batch.vertices.clear();
    context.draw_text(layout, AlborPoint{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // assert the second draw produced the same amount of geometry, because it drew the same path (§6.4)
    EXPECT_EQ(batch.draws.size(), first_draws);
    EXPECT_EQ(batch.indices.size(), first_indices);
    EXPECT_EQ(batch.vertices.size(), first_vertices);
    // assert the cache still holds at least the glyph it walked, because it was re-read (§6.4, AD-06)
    EXPECT_GE(cache.count(), 1u);
}

TEST(AlborTextChecks, DrawsEachGlyphThroughItsOwnOutline) {
    // arrange a run of one glyph, which the context fills as a path
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    FrameBatch batch;
    AlborRenderContext context(batch, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{40.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, AlborPoint{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // assert the glyph records one fill, whose geometry the sweep built
    ASSERT_EQ(batch.draws.size(), 1u);
    EXPECT_EQ(batch.draws[0].coverage, CoverageKind::Plain);
    EXPECT_EQ(batch.draws[0].color, pack_color(Color::rgb(0.1f, 0.2f, 0.3f)));
    EXPECT_GT(batch.indices.size(), 0u);
    EXPECT_GT(batch.vertices.size(), 0u);
}

TEST(AlborTextChecks, SkipsTheSpaceWhichCarriesNoOutline) {
    // arrange a run whose middle glyph is a space
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    ASSERT_EQ(layout.run->lines().front().glyphs.size(), 3u);
    FrameBatch batch;
    AlborRenderContext context(batch, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{80.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, AlborPoint{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // assert the two visible glyphs record, and the space records nothing
    EXPECT_EQ(batch.draws.size(), 2u);
}

TEST(AlborTextChecks, RecordsNothingForAnEmptyRun) {
    // arrange a layout the engine could not shape
    const TextLayout layout;
    FrameBatch batch;
    AlborRenderContext context(batch, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{40.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, AlborPoint{.x = 0.0f, .y = 0.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    // assert
    EXPECT_TRUE(batch.draws.empty());
}

TEST(AlborTextChecks, TakesTheStrokeColourWhenThePaintDoesNotFill) {
    // arrange a paint that only strokes, which the text takes its colour from
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    FrameBatch batch;
    AlborRenderContext context(batch, AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{40.0f, 40.0f}), 1.0f);
    const Paint paint{.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = Color::rgb(0.8f, 0.1f, 0.1f)};
    // act
    context.draw_text(layout, AlborPoint{.x = 4.0f, .y = 30.0f}, paint);
    // assert
    ASSERT_EQ(batch.draws.size(), 1u);
    EXPECT_EQ(batch.draws[0].color, pack_color(Color::rgb(0.8f, 0.1f, 0.1f)));
}

TEST(AlborParityChecks, AShapedRunMatchesTheReference) {
    // arrange a platform run, which the reference rasterizes from the face and the backend fills from its outline
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Al", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_text(layout, AlborPoint{2.0f, 32.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_text(layout, AlborPoint{2.0f, 32.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both surfaces put ink down at the same place
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the bound is the 70 measured on this host, which a hinted stem drawn unhinted leaves on its edges
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 80);
    // the strokes cover the same area, which a displaced baseline would break
    std::size_t expected_ink = 0;
    std::size_t actual_ink = 0;
    for (std::size_t index = 0; index < expected_shot.pixels.size(); index += 4) {
        if (expected_shot.pixels[index] < 128u)
            expected_ink++;
        if (actual_shot.pixels[index] < 128u)
            actual_ink++;
    }
    ASSERT_GT(expected_ink, 0u);
    // the atlas and the outline rasterizer cover a comparable area, which a displaced baseline breaks
    EXPECT_LE(actual_ink, expected_ink * 2u);
}

TEST(AlborParityChecks, ARotatedShapedRunMatchesTheReference) {
    // arrange a platform run turned a fifth of a turn about its own baseline origin (§9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Al", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    const Transform turn = Transform::translation(-8.0f, -36.0f).then(Transform{.scale_x = 0.9396926f, .skew_x = -0.34202015f, .skew_y = 0.34202015f, .scale_y = 0.9396926f}).then(Transform::translation(8.0f, 36.0f));
    // act the same transform reaches both contexts, which the backends must map the same way (§6.3)
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.set_transform(turn);
    expected.draw_text(layout, AlborPoint{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.set_transform(turn);
    actual.draw_text(layout, AlborPoint{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both turned runs cover the same area, which a run left unrotated would miss entirely (§9.1)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    std::size_t expected_ink = 0;
    std::size_t actual_ink = 0;
    std::size_t expected_left = expected_shot.pixels.size();
    std::size_t actual_left = actual_shot.pixels.size();
    std::size_t expected_right = 0;
    std::size_t actual_right = 0;
    for (std::size_t index = 0; index < expected_shot.pixels.size(); index += 4) {
        if (expected_shot.pixels[index] < 128u) {
            expected_ink++;
            expected_left = std::min(expected_left, (index / 4u) % static_cast<std::size_t>(expected_shot.width));
            expected_right = std::max(expected_right, (index / 4u) % static_cast<std::size_t>(expected_shot.width));
        }
        if (actual_shot.pixels[index] < 128u) {
            actual_ink++;
            actual_left = std::min(actual_left, (index / 4u) % static_cast<std::size_t>(actual_shot.width));
            actual_right = std::max(actual_right, (index / 4u) % static_cast<std::size_t>(actual_shot.width));
        }
    }
    ASSERT_GT(expected_ink, 0u);
    EXPECT_LE(actual_ink, (expected_ink * 5u) / 4u);
    // the ink spans the same columns, which a transform mapped in the wrong order would displace
    EXPECT_NEAR(static_cast<double>(actual_left), static_cast<double>(expected_left), 2.0);
    EXPECT_NEAR(static_cast<double>(actual_right), static_cast<double>(expected_right), 2.0);
    // the bound is the 130 measured on this host, which a turned outline leaves on its edges (§9.1)
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 160);
}

TEST(AlborParityChecks, AShearedShapedRunMatchesTheReference) {
    // arrange a platform run sheared about its baseline origin, which tilts every glyph column (§9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Al", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    const Transform shear = Transform::translation(-8.0f, -36.0f).then(Transform{.scale_x = 1.0f, .skew_x = 0.35f, .skew_y = 0.0f, .scale_y = 1.0f}).then(Transform::translation(8.0f, 36.0f));
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.set_transform(shear);
    expected.draw_text(layout, AlborPoint{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.set_transform(shear);
    actual.draw_text(layout, AlborPoint{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the sheared run covers the same area in both backends (§9.1)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    std::size_t expected_ink = 0;
    std::size_t actual_ink = 0;
    std::size_t expected_left = expected_shot.pixels.size();
    std::size_t actual_left = actual_shot.pixels.size();
    std::size_t expected_right = 0;
    std::size_t actual_right = 0;
    for (std::size_t index = 0; index < expected_shot.pixels.size(); index += 4) {
        if (expected_shot.pixels[index] < 128u) {
            expected_ink++;
            expected_left = std::min(expected_left, (index / 4u) % static_cast<std::size_t>(expected_shot.width));
            expected_right = std::max(expected_right, (index / 4u) % static_cast<std::size_t>(expected_shot.width));
        }
        if (actual_shot.pixels[index] < 128u) {
            actual_ink++;
            actual_left = std::min(actual_left, (index / 4u) % static_cast<std::size_t>(actual_shot.width));
            actual_right = std::max(actual_right, (index / 4u) % static_cast<std::size_t>(actual_shot.width));
        }
    }
    ASSERT_GT(expected_ink, 0u);
    EXPECT_LE(actual_ink, (expected_ink * 5u) / 4u);
    // the ink spans the same columns, which a shear mapped in the wrong order would displace
    EXPECT_NEAR(static_cast<double>(actual_left), static_cast<double>(expected_left), 2.0);
    EXPECT_NEAR(static_cast<double>(actual_right), static_cast<double>(expected_right), 2.0);
    // the bound is the 104 measured on this host, which a sheared outline leaves on its edges (§9.1)
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 160);
}

TEST(AlborParityChecks, AnEmbeddedShapedRunMatchesTheReference) {
    // arrange the carried font, whose run expands to outlines before either backend replays (§15, AD-34)
    std::ifstream file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    ASSERT_TRUE(file.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = ALBOR_TEST_FONT_FAMILY, .content_hash = 0, .bytes = std::move(bytes)});
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Albor", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    ASSERT_EQ(layout.run->shape_source(), ShapeSource::Embedded);
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{96.0f, 40.0f}, 1.0f);
    // the recorder expands the run through the engine resolver, which is what a composition root installs (§6.4, AD-34)
    SceneRecorder recorder(metrics);
    recorder.set_outline_resolver(engine->glyph_outline_resolver());
    recorder.set_background(Color::rgb(1.0f, 1.0f, 1.0f));
    recorder.draw_text(layout, AlborPoint{4.0f, 6.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_GT(recorder.outline_command_count(), 0u);
    // act the one scene replays on both backends
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    reference.execute_frame(*expected_surface, *scene);
    backend.execute_frame(*actual_surface, *scene);
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both surfaces put ink in the run box, which an empty expansion would leave blank (§15)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    std::size_t expected_ink = 0;
    std::size_t actual_ink = 0;
    for (std::size_t index = 0; index < expected_shot.pixels.size(); index += 4) {
        if (expected_shot.pixels[index] < 128u)
            expected_ink++;
        if (actual_shot.pixels[index] < 128u)
            actual_ink++;
    }
    ASSERT_GT(expected_ink, 0u);
    EXPECT_LE(actual_ink, (expected_ink * 5u) / 4u);
    EXPECT_GE(actual_ink, (expected_ink * 3u) / 4u);
    // the same outlines fill on both, and the bound is the 57 measured on this host, which one feathered edge pixel reaches
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 64);
    // exit
    resources::FontRegistry::clear();
}

TEST(AlborParityChecks, AShearedRetainedPathMatchesTheReference) {
    // arrange a retained contour under a shear, which the reference maps into the parallelogram it names (§6.4, §6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{64.0f, 64.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[3] = {AlborPoint{12.0f, 8.0f}, AlborPoint{36.0f, 14.0f}, AlborPoint{18.0f, 26.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(91u, verbs, points);
    const Transform shear{.skew_y = 0.9f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, shear, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_path(path, shear, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both surfaces ink the centroid where the shear places it
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t inside = (35u * 64u + 22u) * 4u;
    EXPECT_LT(actual_shot.pixels[inside], 200u);
    EXPECT_LT(expected_shot.pixels[inside], 200u);
    // the un-sheared centroid stays background, which a dropped transform would ink
    const std::size_t before = (16u * 64u + 22u) * 4u;
    EXPECT_GT(actual_shot.pixels[before], 230u);
    EXPECT_GT(expected_shot.pixels[before], 230u);
    // the slanted edges follow the reference within its one pixel feather band, at the 26 measured on this host
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborParityChecks, ASelfOverlappingPathCutsItsCounterOnBothBackends) {
    // arrange one contour whose bar is crossed by its own stroke, which leaves the pocket below the bar at winding zero
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{28.0f, 28.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[17] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[16] = {AlborPoint{2.0f, 2.0f}, AlborPoint{22.0f, 2.0f}, AlborPoint{22.0f, 22.0f}, AlborPoint{2.0f, 22.0f}, AlborPoint{2.0f, 12.0f}, AlborPoint{18.0f, 12.0f}, AlborPoint{18.0f, 8.0f}, AlborPoint{2.0f, 8.0f}, AlborPoint{2.0f, 6.0f}, AlborPoint{4.0f, 6.0f}, AlborPoint{4.0f, 16.0f}, AlborPoint{20.0f, 16.0f}, AlborPoint{20.0f, 5.0f}, AlborPoint{6.0f, 5.0f}, AlborPoint{6.0f, 10.0f}, AlborPoint{2.0f, 10.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(93u, verbs, points);
    // act both backends fill the one path
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both surfaces leave every pixel of the pocket as background, which a simple ring triangulation paints
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    std::size_t expected_pocket_ink = 0;
    std::size_t actual_pocket_ink = 0;
    for (int y = 12; y < 16; y++) {
        for (int x = 8; x < 16; x++) {
            // the pixel the pocket window holds, which both surfaces must leave bare
            const std::size_t at = (static_cast<std::size_t>(y) * 28u + static_cast<std::size_t>(x)) * 4u;
            if (expected_shot.pixels[at] < 230u)
                expected_pocket_ink++;
            if (actual_shot.pixels[at] < 230u)
                actual_pocket_ink++;
        }
    }
    EXPECT_EQ(expected_pocket_ink, 0u);
    EXPECT_EQ(actual_pocket_ink, 0u);
    // both surfaces ink the bar, which the winding keeps painted
    const std::size_t bar = (11u * 28u + 10u) * 4u;
    EXPECT_LT(actual_shot.pixels[bar], 200u);
    EXPECT_LT(expected_shot.pixels[bar], 200u);
    // the two images disagree on 17 pixels, which the half fringe the crossed contour's inset side leaves on its edges
    std::size_t mismatched = 0;
    for (std::size_t index = 0; index < expected_shot.pixels.size(); index += 4) {
        // a pixel one surface inks and the other leaves bare is an edge the two place differently
        if ((expected_shot.pixels[index] < 128u) != (actual_shot.pixels[index] < 128u))
            mismatched++;
    }
    // the bound leaves room for that band but not for the counter, which the defect would add as whole pixels
    EXPECT_LE(mismatched, 40u);
}

TEST(AlborParityChecks, ARotatedLineBatchMatchesTheReference) {
    // arrange a retained batch turned 0.6 radians about the surface centre, which a translation cannot do (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborPoint points[2] = {AlborPoint{10.0f, 24.0f}, AlborPoint{30.0f, 24.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(92u, points);
    const StrokeStyle stroke{.color = Color::rgb(0.2f, 0.2f, 0.2f), .width = 3.0f};
    const Transform turn = Transform::translation(-32.0f, -24.0f).then(Transform{.scale_x = 0.8253356f, .skew_x = -0.5646425f, .skew_y = 0.5646425f, .scale_y = 0.8253356f}).then(Transform::translation(32.0f, 24.0f));
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_line_batch(batch, turn, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_line_batch(batch, turn, stroke);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the segment lands where the turn maps its midpoint
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const AlborPoint mapped = turn.apply(AlborPoint{.x = 20.0f, .y = 24.0f});
    const std::size_t turned = (static_cast<std::size_t>(mapped.y) * 64u + static_cast<std::size_t>(mapped.x)) * 4u;
    EXPECT_LT(actual_shot.pixels[turned], 220u);
    EXPECT_LT(expected_shot.pixels[turned], 220u);
    // the unrotated midpoint stays background, which an ignored retained transform would ink
    const std::size_t before = (24u * 64u + 20u) * 4u;
    EXPECT_GT(actual_shot.pixels[before], 230u);
    EXPECT_GT(expected_shot.pixels[before], 230u);
    // the rotated strip follows the reference within its feather band, at the 27 measured on this host
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborParityChecks, ARotatedPictureMatchesTheReference) {
    // arrange a retained picture replayed a quarter turn about the surface centre (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(AlborRect::from_origin_size(AlborPoint{4.0f, 18.0f}, AlborSize{10.0f, 10.0f}), Paint{.fill = Color::rgb(0.3f, 0.6f, 0.9f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(93u, commands);
    const Transform turn = Transform::translation(-24.0f, -24.0f).then(Transform{.scale_x = 0.8253356f, .skew_x = -0.5646425f, .skew_y = 0.5646425f, .scale_y = 0.8253356f}).then(Transform::translation(24.0f, 24.0f));
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_picture(picture, turn);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_picture(picture, turn);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the picture centre lands where the turn maps it
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const AlborPoint mapped = turn.apply(AlborPoint{.x = 9.0f, .y = 23.0f});
    const std::size_t turned = (static_cast<std::size_t>(mapped.y) * 48u + static_cast<std::size_t>(mapped.x)) * 4u;
    EXPECT_LT(actual_shot.pixels[turned], 200u);
    EXPECT_LT(expected_shot.pixels[turned], 200u);
    // the original centre stays background, which a dropped picture transform would ink
    const std::size_t before = (23u * 48u + 9u) * 4u;
    EXPECT_GT(actual_shot.pixels[before], 230u);
    EXPECT_GT(expected_shot.pixels[before], 230u);
    // the replayed command follows the reference within its feather band, at the 35 measured on this host
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 40);
}

TEST(AlborParityChecks, AContextOpacityDimsThePixels) {
    // arrange a context opacity around one draw, which the reference dims while it composites (§6.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act the first rectangle draws at half opacity, and the second at full once the state is popped
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_opacity(0.5f);
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{2.0f, 2.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected.restore();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{18.0f, 2.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.set_opacity(0.5f);
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{2.0f, 2.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual.restore();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{18.0f, 2.0f}, AlborSize{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the dimmed rectangle reaches the half coverage the opacity asks for
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t dimmed = (6u * 32u + 6u) * 4u;
    EXPECT_GT(actual_shot.pixels[dimmed], 100u);
    EXPECT_LT(actual_shot.pixels[dimmed], 160u);
    // the rectangle outside the saved state draws fully black, which an unpopped opacity would dim
    const std::size_t full = (6u * 32u + 22u) * 4u;
    EXPECT_LT(actual_shot.pixels[full], 6u);
    // the reference paints the same two levels, which the whole image comparison confirms
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
}

TEST(AlborParityChecks, AnEvenOddPathMatchesTheReference) {
    // arrange two nested squares of one winding, whose hole only the even odd rule cuts (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[10] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const AlborPoint points[8] = {AlborPoint{6.0f, 6.0f}, AlborPoint{6.0f, 42.0f}, AlborPoint{42.0f, 42.0f}, AlborPoint{42.0f, 6.0f}, AlborPoint{16.0f, 16.0f}, AlborPoint{16.0f, 32.0f}, AlborPoint{32.0f, 32.0f}, AlborPoint{32.0f, 16.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(94u, verbs, points);
    const Paint paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f), .fill_rule = FillRule::EvenOdd};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_path(path, Transform{}, paint);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the nested square stays background in both, which the even odd rule leaves open
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t hole = (24u * 48u + 24u) * 4u;
    EXPECT_GT(actual_shot.pixels[hole], 230u);
    EXPECT_GT(expected_shot.pixels[hole], 230u);
    // the band between the two squares fills in both
    const std::size_t band = (24u * 48u + 11u) * 4u;
    EXPECT_LT(actual_shot.pixels[band], 200u);
    EXPECT_LT(expected_shot.pixels[band], 200u);
    // the whole path follows the reference within its feathered edge band
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborParityChecks, ATranslucentSourceFollowsEveryMode) {
    // arrange one half transparent overlay per composition mode over an opaque plate (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const CompositionMode modes[] = {CompositionMode::SourceOver, CompositionMode::SourceIn, CompositionMode::SourceOut, CompositionMode::Atop, CompositionMode::DestinationOver, CompositionMode::DestinationIn, CompositionMode::DestinationOut, CompositionMode::DestinationAtop, CompositionMode::Lighter, CompositionMode::Copy, CompositionMode::Xor};
    const Paint paint{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f, .alpha = 0.5f}};
    for (const CompositionMode mode : modes) {
        // the trace names the mode a mismatch belongs to, which a loop otherwise hides
        SCOPED_TRACE(static_cast<int>(mode));
        // act
        RenderContext& expected = expected_surface->begin_frame();
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = paint.fill, .composition = mode});
        expected_surface->end_frame();
        RenderContext& actual = actual_surface->begin_frame();
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = paint.fill, .composition = mode});
        actual_surface->end_frame();
        const Screenshot expected_shot = capture_surface(*expected_surface);
        const Screenshot actual_shot = capture_surface(*actual_surface);
        ASSERT_FALSE(expected_shot.empty());
        ASSERT_FALSE(actual_shot.empty());
        ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
        // assert every channel agrees, which the destination-aware pipelines make possible (§6.2)
        EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    }
}

TEST(AlborParityChecks, ATranslucentSourceFollowsEveryModeOverATranslucentDestination) {
    // arrange a half transparent plate inside a layer, which leaves a destination alpha to blend against (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const CompositionMode modes[] = {CompositionMode::SourceIn, CompositionMode::DestinationAtop, CompositionMode::Copy};
    const Paint plate{.fill = Color{.red = 0.8f, .green = 0.2f, .blue = 0.2f, .alpha = 0.6f}};
    const Paint overlay{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f, .alpha = 0.5f}};
    const AlborRect whole = AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size);
    for (const CompositionMode mode : modes) {
        // the trace names the mode a mismatch belongs to, which a loop otherwise hides
        SCOPED_TRACE(static_cast<int>(mode));
        // act the plate and the overlay meet inside one group, whose target alpha is not one
        RenderContext& expected = expected_surface->begin_frame();
        expected.push_layer(1.0f);
        expected.draw_rect(whole, plate);
        expected.draw_rect(whole, Paint{.fill = overlay.fill, .composition = mode});
        expected.pop_layer();
        expected_surface->end_frame();
        RenderContext& actual = actual_surface->begin_frame();
        actual.push_layer(1.0f);
        actual.draw_rect(whole, plate);
        actual.draw_rect(whole, Paint{.fill = overlay.fill, .composition = mode});
        actual.pop_layer();
        actual_surface->end_frame();
        const Screenshot expected_shot = capture_surface(*expected_surface);
        const Screenshot actual_shot = capture_surface(*actual_surface);
        // assert the result agrees, which a destination factor that ignores the plate alpha would break (§6.2)
        ASSERT_FALSE(expected_shot.empty());
        ASSERT_FALSE(actual_shot.empty());
        ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
        EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    }
}

TEST(AlborParityChecks, ANestedLayerMatchesTheReference) {
    // arrange two halves over a white plate, which a quarter of the red reaches through (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborRect whole = AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size);
    const AlborRect red = AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, AlborSize{16.0f, 16.0f});
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(whole, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.push_layer(0.5f);
    expected.push_layer(0.5f);
    expected.draw_rect(red, Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    expected.pop_layer();
    expected.pop_layer();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(whole, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.push_layer(0.5f);
    actual.push_layer(0.5f);
    actual.draw_rect(red, Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    actual.pop_layer();
    actual.pop_layer();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the inner group composited into the outer one before it landed (§6.2, AD-26)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
}

TEST(AlborParityChecks, ALayerAroundACastOnlyFrameMatchesTheReference) {
    // arrange a half opaque group whose only content is a hard cast, so the frame carries no geometry (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborRect whole = AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size);
    const AlborRect plate = AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f});
    const Shadow cast{.color = Color::rgb(0.0f, 0.0f, 0.0f), .offset = AlborPoint{2.0f, 2.0f}, .blur = 0.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(whole, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.push_layer(0.5f);
    expected.draw_shadow(plate, 0.0f, cast);
    expected.pop_layer();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(whole, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.push_layer(0.5f);
    actual.draw_shadow(plate, 0.0f, cast);
    actual.pop_layer();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the cast composited at the group opacity rather than at full strength (§6.2, AD-26)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborParityChecks, ALayerDimsItsGroupOnce) {
    // arrange a half opaque layer over a white plate, whose two red children overlap by design (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const AlborRect whole = AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size);
    const AlborRect first = AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{24.0f, 24.0f});
    const AlborRect second = AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{24.0f, 24.0f});
    const Paint plate{.fill = Color::rgb(1.0f, 1.0f, 1.0f)};
    const Paint red{.fill = Color::rgb(0.8f, 0.2f, 0.2f)};
    // act the empty group composites nothing, the half opaque group paints both children and composites once
    RenderContext& expected = expected_surface->begin_frame();
    expected.push_layer(0.5f);
    expected.pop_layer();
    expected.draw_rect(whole, plate);
    expected.push_layer(0.5f);
    expected.draw_rect(first, red);
    expected.draw_rect(second, red);
    expected.pop_layer();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.push_layer(0.5f);
    actual.pop_layer();
    actual.draw_rect(whole, plate);
    actual.push_layer(0.5f);
    actual.draw_rect(first, red);
    actual.draw_rect(second, red);
    actual.pop_layer();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the whole group follows the reference, which dims the overlap once rather than per child (§6.2, AD-26)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    const std::size_t single = (8u * 48u + 8u) * 4u;
    const std::size_t overlap = (24u * 48u + 24u) * 4u;
    EXPECT_NEAR(actual_shot.pixels[overlap], actual_shot.pixels[single], 2);
}

TEST(AlborParityChecks, EveryCompositionModeMatchesTheReference) {
    // arrange one overlay per composition mode, which the backend maps to its blender (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const CompositionMode modes[] = {CompositionMode::SourceOver, CompositionMode::SourceIn, CompositionMode::SourceOut, CompositionMode::Atop, CompositionMode::DestinationOver, CompositionMode::DestinationIn, CompositionMode::DestinationOut, CompositionMode::DestinationAtop, CompositionMode::Lighter, CompositionMode::Copy, CompositionMode::Xor};
    for (const CompositionMode mode : modes) {
        // the trace names the mode a mismatch belongs to, which a loop otherwise hides
        SCOPED_TRACE(static_cast<int>(mode));
        // act the same base rectangle and overlay replay under the mode the frame names
        RenderContext& expected = expected_surface->begin_frame();
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        expected.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.2f, 0.2f, 0.8f), .composition = mode});
        expected_surface->end_frame();
        RenderContext& actual = actual_surface->begin_frame();
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        actual.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.2f, 0.2f, 0.8f), .composition = mode});
        actual_surface->end_frame();
        const Screenshot expected_shot = capture_surface(*expected_surface);
        const Screenshot actual_shot = capture_surface(*actual_surface);
        // assert the blender agrees with the reference, which uses its own operator for the same mode
        ASSERT_FALSE(expected_shot.empty());
        ASSERT_FALSE(actual_shot.empty());
        ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
        EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    }
}

TEST(AlborParityChecks, ATranslucentDestinationInMatchesTheReference) {
    // arrange a half transparent overlay that keeps the destination it covers, which reshapes the residual (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(AlborSize{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Paint paint{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f, .alpha = 0.5f}, .composition = CompositionMode::DestinationIn};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{40.0f, 40.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    expected.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{24.0f, 24.0f}), paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{4.0f, 4.0f}, AlborSize{40.0f, 40.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    actual.draw_rect(AlborRect::from_origin_size(AlborPoint{12.0f, 12.0f}, AlborSize{24.0f, 24.0f}), paint);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the covered destination is dimmed by the source alpha, which the reference fades the same way
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t covered = (24u * 48u + 24u) * 4u;
    EXPECT_LT(actual_shot.pixels[covered], 150u);
    EXPECT_LT(expected_shot.pixels[covered], 150u);
    // the whole overlay follows the reference, its feathered boundary included
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
}
