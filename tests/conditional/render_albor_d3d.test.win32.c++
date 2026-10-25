// the direct3d backend, which the device, the encoders and a real window drive (§6.2, §6.3, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/image_resource.h"
#include "render/picture_resource.h"
#include "render/render-albor/albor_d3d_diagnostics.h"
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

TEST(AlborShaderLibraryChecks, CarriesTheCompiledDirect3DContainer) {
    // arrange the bytes the build embedded, which are the shader set the device loads (AD-16)
    const std::span<const std::byte> data = d3d_shader_data();
    // act
    ASSERT_GT(data.size(), 8u);
    // assert the header names the container format the backend parses (§6.3)
    EXPECT_EQ(std::to_integer<unsigned>(data[0]), 'A');
    EXPECT_EQ(std::to_integer<unsigned>(data[1]), 'L');
    EXPECT_EQ(std::to_integer<unsigned>(data[2]), 'B');
    EXPECT_EQ(std::to_integer<unsigned>(data[3]), 'D');
    EXPECT_EQ(std::to_integer<unsigned>(data[4]), 'X');
    EXPECT_EQ(std::to_integer<unsigned>(data[5]), 'B');
    EXPECT_EQ(std::to_integer<unsigned>(data[6]), 'C');
    EXPECT_EQ(std::to_integer<unsigned>(data[7]), '1');
    // walk the six length prefixed sections, so a truncated artifact fails here rather than at bring up (§6.3)
    std::size_t offset = 8;
    for (int section = 0; section < 6; section++) {
        ASSERT_LE(offset + 4, data.size());
        const auto* octets = reinterpret_cast<const std::uint8_t*>(data.data() + offset);
        const std::size_t length = static_cast<std::size_t>(octets[0]) | (static_cast<std::size_t>(octets[1]) << 8) | (static_cast<std::size_t>(octets[2]) << 16) | (static_cast<std::size_t>(octets[3]) << 24);
        offset += 4;
        // every entry point compiled a body, which an empty section would contradict (§6.3)
        EXPECT_GT(length, 0u);
        ASSERT_LE(offset + length, data.size());
        offset += length;
    }
    // the container ends with the sixth section, so a trailing byte belongs to another build (§6.3)
    EXPECT_EQ(offset, data.size());
}

TEST(AlborRendererBackendChecks, BringsUpTheDirect3DDevice) {
    // arrange
    AlborRendererBackend backend;
    // act
    const bool ready = backend.ready();
    // assert a host with no direct3d device skips, so a platform gap is not read as a product defect (§6.2)
    if (!ready)
        GTEST_SKIP() << "no direct3d device on this host";
    EXPECT_TRUE(ready);
}

TEST(AlborRendererBackendChecks, BuildsAnOffscreenSurfaceWithoutAWindow) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 24.0f}, 2.0f);
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    // assert
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    // the surface and the backend agree on readiness, so a half built device cannot hand out a surface (§6.2)
    EXPECT_EQ(backend.ready(), surface->valid());
    EXPECT_EQ(surface->metrics(), metrics);
}

TEST(AlborRendererBackendChecks, ReusesTheBatchBuffersAcrossSteadyFrames) {
    // arrange a backend and an offscreen surface, which needs no window to draw a frame (§6.2)
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{320.0f, 200.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // arrange one immutable scene, which every frame replays unchanged (§6.1)
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{120.0f, 80.0f}), Paint{.fill = Color::rgb(0.2f, 0.4f, 0.8f)}));
    // act a warmup that fills every retained buffer, which a steady frame then reuses (AD-06)
    for (int frame = 0; frame < 8; frame++)
        backend.execute_frame(*surface, scene);
    // arrange the allocations the warmup made, which the steady frames must not raise (AD-06)
    const std::uint64_t allocations = backend.batch_buffer_allocations();
    ASSERT_GT(allocations, 0u);
    // act a steady stream of the same scene, which is the shape a manual drag has every frame (§6.1)
    for (int frame = 0; frame < 120; frame++)
        backend.execute_frame(*surface, scene);
    // assert every steady frame reused the buffers the warmup allocated, which a per frame allocation would raise (AD-06)
    EXPECT_EQ(backend.batch_buffer_allocations(), allocations);
}

TEST(AlborRendererBackendChecks, ClearsAnEmptyOffscreenFrameToTransparent) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    const std::uint64_t before = d3d_presented_frames();
    surface->begin_frame();
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert an offscreen frame presents nothing, which the process counter shows (§6.3)
    EXPECT_EQ(d3d_presented_frames(), before);
    // assert every pixel is the transparent clear a frame with no command leaves (§6.1)
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    for (std::size_t pixel = 0; pixel < 16u; pixel++) {
        EXPECT_EQ(pixels[pixel * 4u + 0u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 1u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 2u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 3u], 0u);
    }
}

TEST(AlborRendererBackendChecks, ReadsBackInCanonicalRgbaOrder) {
    // arrange a colour with three distinct channels, which a rotation or a lost channel would expose (AD-26)
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const FrameScene scene(metrics, Color::rgb(0.25f, 0.5f, 0.75f));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert each channel lands in its own byte, which a swap or a rotation would misplace (AD-26)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    EXPECT_NEAR(pixels[0], 64u, 1u);
    EXPECT_NEAR(pixels[1], 128u, 1u);
    EXPECT_NEAR(pixels[2], 191u, 1u);
    EXPECT_EQ(pixels[3], 255u);
    const std::size_t last = pixels.size() - 4u;
    // the last pixel of the last row carries the same channels, so the whole target was covered
    EXPECT_NEAR(pixels[last + 0u], 64u, 1u);
    EXPECT_NEAR(pixels[last + 1u], 128u, 1u);
    EXPECT_NEAR(pixels[last + 2u], 191u, 1u);
    EXPECT_EQ(pixels[last + 3u], 255u);
}

TEST(AlborRendererBackendChecks, RefusesAReadWhileAFrameIsInFlight) {
    // arrange a surface with one finished frame, so the refusal is the flight and not the first frame (§6.3)
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    surface->begin_frame();
    surface->end_frame();
    ASSERT_FALSE(surface->read_pixels().empty());
    // act
    surface->begin_frame();
    const std::vector<std::uint8_t> during = surface->read_pixels();
    // assert
    EXPECT_TRUE(during.empty());
    surface->end_frame();
    EXPECT_FALSE(surface->read_pixels().empty());
}

TEST(AlborRendererBackendChecks, IgnoresAnEndWithoutABegin) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->end_frame();
    // assert no frame finished, so the read reports nothing (§6.1)
    EXPECT_TRUE(surface->read_pixels().empty());
}

TEST(AlborRendererBackendChecks, RasterizesAnOpaqueRectangleOverTheBackground) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    EXPECT_EQ(pixels[centre + 1u], 255u);
    EXPECT_EQ(pixels[centre + 2u], 0u);
    EXPECT_EQ(pixels[centre + 3u], 255u);
}

TEST(AlborRendererBackendChecks, AntialiasesAFractionalEdge) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.5f, 0.0f}, Size{14.5f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 4u * 4u);
    // the first column is half covered by the feathered edge, which halves the red over black
    EXPECT_GT(pixels[0], 118u);
    EXPECT_LT(pixels[0], 138u);
    // the second column is fully covered, because the fringe ended inside it
    EXPECT_EQ(pixels[4], 255u);
    // the far column the rectangle never reaches stays the background
    const std::size_t last = (3u * 16u + 15u) * 4u;
    EXPECT_EQ(pixels[last], 0u);
}

TEST(AlborRendererBackendChecks, CutsADrawAtTheClipItCarries) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 8.0f}));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert
    ASSERT_EQ(pixels.size(), 16u * 8u * 4u);
    // inside the clip the fill lands, which the red channel proves
    const std::size_t inside = (4u * 16u + 3u) * 4u;
    EXPECT_EQ(pixels[inside], 255u);
    EXPECT_EQ(pixels[inside + 3u], 255u);
    // outside the clip the background stands, because the scissor mask cut the draw away
    const std::size_t outside = (4u * 16u + 13u) * 4u;
    EXPECT_EQ(pixels[outside], 0u);
    EXPECT_EQ(pixels[outside + 3u], 255u);
}

TEST(AlborRendererBackendChecks, DimsADrawByItsOpacity) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 8.0f}));
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
    EXPECT_EQ(pixels[centre + 3u], 255u);
}

TEST(AlborRendererBackendChecks, ResizesTheOffscreenTarget) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics small = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const WindowMetrics large = WindowMetrics::from_logical(Size{8.0f, 6.0f}, 1.0f);
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
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    backend.resize_surface(*surface, WindowMetrics{});
    // assert the refusal kept the extent and the target, which a dropped target would read back short (§6.2)
    EXPECT_EQ(surface->metrics(), metrics);
    const FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    EXPECT_EQ(pixels[0], 255u);
}

TEST(AlborRendererBackendChecks, KeepsTheTargetWhenResizedToAnEmptyExtent) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->resize(WindowMetrics{});
    // assert the target the refusal kept still draws and reads back at the extent it had (§6.2)
    EXPECT_EQ(surface->metrics(), metrics);
    const FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    backend.execute_frame(*surface, scene);
    std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    EXPECT_EQ(pixels[0], 255u);
    // a later resize to the same extent takes the target path again, which a lost target would refuse (§6.2)
    surface->resize(metrics);
    backend.execute_frame(*surface, scene);
    pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    EXPECT_EQ(pixels[0], 255u);
}

TEST(AlborParityChecks, AnAlignedRectangleMatchesTheReference) {
    // arrange the same scene through the software reference and through this backend
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.0f, 0.8f, 0.2f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert an aligned rectangle has no feathered edge to disagree on, so it agrees exactly at its interior
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    const std::size_t interior = (16u * 32u + 16u) * 4u;
    EXPECT_EQ(expected.pixels[interior + 0u], actual.pixels[interior + 0u]);
    EXPECT_EQ(expected.pixels[interior + 1u], actual.pixels[interior + 1u]);
    EXPECT_EQ(expected.pixels[interior + 2u], actual.pixels[interior + 2u]);
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborParityChecks, AFeatheredEdgeMatchesTheReferenceWithinTolerance) {
    // arrange a rectangle whose edges land on fractional pixels
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{6.3f, 6.7f}, Size{19.4f, 18.6f}), Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 2.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
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
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.clip_rounded_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{24.0f, 24.0f}), 12.0f);
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.clip_rounded_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{24.0f, 24.0f}), 12.0f);
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const std::size_t corner = (5u * 32u + 5u) * 4u;
    EXPECT_EQ(actual_shot.pixels[corner], 255u);
    EXPECT_EQ(expected_shot.pixels[corner], 255u);
    // the centre of the clip is covered by the black rectangle in both
    const std::size_t centre = (16u * 32u + 16u) * 4u;
    EXPECT_EQ(actual_shot.pixels[centre], 0u);
    EXPECT_EQ(expected_shot.pixels[centre], 0u);
    // the whole rounded mask follows the reference, at the 31 measured on this host along its arcs
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 40);
}

TEST(AlborParityChecks, ReusesAnUploadedTextureAcrossFrames) {
    // arrange the same retained image drawn on two consecutive frames
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const std::uint8_t pixels[4] = {0, 0, 255, 255};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(41u, 1u, 1u, ImageFormat::RGBA8, pixels);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    command.set_clip(Rect::from_origin_size(Point{7.3f, 9.6f}, Size{17.2f, 12.1f}));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_transform(Transform::translation(5.0f, 5.0f));
    expected.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{12.0f, 12.0f}));
    expected.set_transform(Transform{});
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected.restore();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.set_transform(Transform::translation(5.0f, 5.0f));
    actual.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{12.0f, 12.0f}));
    actual.set_transform(Transform{});
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
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

TEST(AlborParityChecks, AFrameRenderedAtAnotherScaleDoublesTheGeometry) {
    // arrange the same logical square at two densities, which the device scale must carry into the geometry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const WindowMetrics denser = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 2.0f);
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> small = backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> large = backend.create_surface(NativeWindowHandle{}, denser);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    FrameScene small_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand small_square = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{4.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    small_square.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    small_scene.add_command(std::move(small_square));
    FrameScene large_scene(denser, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand large_square = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{4.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    large_square.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, denser.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f});
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

TEST(AlborParityChecks, AHardShadowMatchesTheReferenceWithinTolerance) {
    // arrange a zero blur shadow, which is a hard offset copy of the caster
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{12.0f, 12.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f), .offset = Point{4.0f, 4.0f}, .blur = 0.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{12.0f, 12.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 1.0f), .offset = Point{4.0f, 4.0f}, .blur = 0.0f});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    // the offset copy lands on the same pixels, which the two agree on within the silhouette edge band
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 8);
    // the cast is black where it falls, which a missing shadow would leave white
    const std::size_t cast = (18u * 32u + 18u) * 4u;
    EXPECT_EQ(actual_shot.pixels[cast], 0u);
    EXPECT_EQ(expected_shot.pixels[cast], 0u);
}

TEST(AlborParityChecks, AStrokedPolylineMatchesTheReference) {
    // arrange a round joined polyline, which exercises the folded coverage ramp
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Point points[3] = {Point{6.0f, 8.0f}, Point{20.0f, 8.0f}, Point{20.0f, 30.0f}};
    const StrokeStyle stroke{.color = Color::rgb(0.1f, 0.1f, 0.9f), .width = 3.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_polyline(points, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Paint paint{.fill = Color::rgb(0.2f, 0.7f, 0.4f), .stroke = Color::rgb(0.0f, 0.0f, 0.0f), .stroke_width = 1.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rounded_rect(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{24.0f, 24.0f}), 8.0f, paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rounded_rect(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{24.0f, 24.0f}), 8.0f, paint);
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[3] = {Point{8.0f, 30.0f}, Point{20.0f, 8.0f}, Point{32.0f, 30.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(51u, verbs, points);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.9f, 0.2f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[10] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[8] = {Point{8.0f, 8.0f}, Point{8.0f, 32.0f}, Point{32.0f, 32.0f}, Point{32.0f, 8.0f}, Point{16.0f, 16.0f}, Point{24.0f, 16.0f}, Point{24.0f, 24.0f}, Point{16.0f, 24.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(53u, verbs, points);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.9f, 0.2f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Point points[4] = {Point{6.0f, 10.0f}, Point{34.0f, 10.0f}, Point{6.0f, 26.0f}, Point{34.0f, 26.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(52u, points);
    const StrokeStyle stroke{.color = Color::rgb(0.2f, 0.2f, 0.2f), .width = 2.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_line_batch(batch, Transform{}, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(0.3f, 0.6f, 0.9f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(53u, commands);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_picture(picture, Transform::translation(12.0f, 14.0f));
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 0.5f)});
    expected.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 1.0f, 0.5f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 0.5f)});
    actual.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 1.0f, 0.5f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_shadow(Rect::from_origin_size(Point{8.0f, 16.0f}, Size{48.0f, 12.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_shadow(Rect::from_origin_size(Point{8.0f, 16.0f}, Size{48.0f, 12.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the shear slants the whole caster, which an axis aligned silhouette cannot follow (§6.2, §6.1)
    const Transform shear{.skew_y = 0.6f};
    const Rect caster = Rect::from_origin_size(Point{16.0f, 16.0f}, Size{40.0f, 16.0f});
    const Shadow cast{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.7f), .offset = Point{0.0f, 0.0f}, .blur = 0.0f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_transform(shear);
    expected.draw_shadow(caster, 0.0f, cast);
    expected.restore();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{24.0f, 24.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::uint8_t pixels[4] = {255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(61u, 1u, 1u, ImageFormat::RGBA8, pixels);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{3.4f, 2.7f}, Size{9.6f, 8.3f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    RenderContext& context = surface->begin_frame();
    context.draw_shadow(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{16.0f, 16.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.8f), .offset = Point{0.0f, 0.0f}, .blur = 4.0f});
    surface->end_frame();
    const Screenshot shot = capture_surface(*surface);
    // assert the cast lands even though the frame carried no geometry at all
    ASSERT_FALSE(shot.empty());
    const std::size_t centre = (16u * 32u + 16u) * 4u;
    const std::size_t inside = (20u * 32u + 20u) * 4u;
    const std::size_t outside = (2u * 32u + 2u) * 4u;
    // the cast centre and a second point inside it carry the shadow alpha, which a stray pixel could not do
    EXPECT_GT(shot.pixels[centre + 3u], 100u);
    EXPECT_GT(shot.pixels[inside + 3u], 100u);
    // a corner four pixels outside the cast stays untouched, so the pass did not smear the frame (§6.2)
    EXPECT_EQ(shot.pixels[outside + 3u], 0u);
}

TEST(AlborParityChecks, KeepsAFrameThatUploadsMoreImagesThanTheCacheHolds) {
    // arrange a surface pair and seventy images, which is more than the texture cache is sized for (§6.4, AD-06)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<std::shared_ptr<const ImageResource>> images;
    for (std::uint64_t index = 0u; index < 70u; index++) {
        // every image is its own identifier and its own colour, so none of them is answered by another (AD-06)
        const std::vector<std::uint8_t> source{static_cast<std::uint8_t>(index * 3u), 60u, 120u, 255u};
        images.push_back(std::make_shared<const ImageResource>(300u + index, 1u, 1u, ImageFormat::RGBA8, source));
    }
    // arrange the first frame, whose seventy uploads all come from the same frame (AD-06)
    FrameScene first(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    for (std::size_t index = 0u; index < images.size(); index++) {
        // the images tile a ten column grid, whose row is the floating quotient rather than the integer one (§6.4)
        const Point origin{.x = 4.0f + static_cast<float>(index % 10u) * 9.0f, .y = 4.0f + (static_cast<float>(index) / 10u) * 9.0f};
        DrawCommand command = DrawCommand::image(images[index], Rect::from_origin_size(origin, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        first.add_command(std::move(command));
    }
    // act the first frame, which fills the cache past its capacity but evicts nothing of its own (AD-06)
    reference.execute_frame(*expected_surface, first);
    backend.execute_frame(*actual_surface, first);
    // act a second frame with one new image, whose store drops the coldest slice the first frame left (AD-06)
    const std::vector<std::uint8_t> fresh{255u, 0u, 255u, 255u};
    const std::shared_ptr<const ImageResource> newer = std::make_shared<const ImageResource>(400u, 1u, 1u, ImageFormat::RGBA8, fresh);
    FrameScene second(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand trigger = DrawCommand::image(newer, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    trigger.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    second.add_command(std::move(trigger));
    reference.execute_frame(*expected_surface, second);
    backend.execute_frame(*actual_surface, second);
    // arrange the third frame, which redraws every image so the evicted slice is uploaded again in any order (AD-06)
    FrameScene third(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    for (std::size_t index = 0u; index < images.size(); index++) {
        const Point origin{.x = 4.0f + static_cast<float>(index % 10u) * 9.0f, .y = 4.0f + (static_cast<float>(index) / 10u) * 9.0f};
        DrawCommand command = DrawCommand::image(images[index], Rect::from_origin_size(origin, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        third.add_command(std::move(command));
    }
    // act the third frame, which a dropped texture answers by uploading it again (AD-06)
    reference.execute_frame(*expected_surface, third);
    backend.execute_frame(*actual_surface, third);
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert every re-uploaded tile keeps its own colour, which a stale or missing texture would not (§6.4, AD-06)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(actual_shot.max_channel_difference(expected_shot), 2);
}
TEST(AlborParityChecks, ADownscaledImageMatchesTheReference) {
    // arrange a checkerboard downscaled eight times, which a mip map keeps from aliasing
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
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
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{2.0f, 2.0f}, Size{12.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[9] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[8] = {Point{8.0f, 8.0f}, Point{34.0f, 8.0f}, Point{34.0f, 34.0f}, Point{26.0f, 34.0f}, Point{26.0f, 16.0f}, Point{16.0f, 16.0f}, Point{16.0f, 34.0f}, Point{8.0f, 34.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(81u, verbs, points);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::path(path, Transform{}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    AlborRenderContext context(batch, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}), 1.0f, &cache);
    // act the glyph is drawn once, which stores it in the cache, and again, which must read it back (§6.4, AD-06)
    context.draw_text(layout, Point{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // the first draw produces the batch content the second draw will produce again (§6.4)
    const std::size_t first_draws = batch.draws.size();
    const std::size_t first_indices = batch.indices.size();
    const std::size_t first_vertices = batch.vertices.size();
    // the cache must now hold the walked glyph, so a second draw can reuse it (§6.4, AD-06)
    EXPECT_GE(cache.count(), 1u);
    batch.draws.clear();
    batch.indices.clear();
    batch.vertices.clear();
    context.draw_text(layout, Point{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
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
    AlborRenderContext context(batch, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, Point{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
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
    AlborRenderContext context(batch, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, Point{.x = 4.0f, .y = 30.0f}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.3f)});
    // assert the two visible glyphs record, and the space records nothing
    EXPECT_EQ(batch.draws.size(), 2u);
}

TEST(AlborTextChecks, RecordsNothingForAnEmptyRun) {
    // arrange a layout the engine could not shape
    const TextLayout layout;
    FrameBatch batch;
    AlborRenderContext context(batch, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}), 1.0f);
    // act
    context.draw_text(layout, Point{.x = 0.0f, .y = 0.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
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
    AlborRenderContext context(batch, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}), 1.0f);
    const Paint paint{.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = Color::rgb(0.8f, 0.1f, 0.1f)};
    // act
    context.draw_text(layout, Point{.x = 4.0f, .y = 30.0f}, paint);
    // assert
    ASSERT_EQ(batch.draws.size(), 1u);
    EXPECT_EQ(batch.draws[0].color, pack_color(Color::rgb(0.8f, 0.1f, 0.1f)));
}

TEST(AlborParityChecks, AShapedRunMatchesTheReference) {
    // arrange a platform run, which the reference rasterizes from the face and the backend fills from its outline
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
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
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_text(layout, Point{2.0f, 32.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_text(layout, Point{2.0f, 32.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
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
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.set_transform(turn);
    expected.draw_text(layout, Point{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.set_transform(turn);
    actual.draw_text(layout, Point{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
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
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.set_transform(shear);
    expected.draw_text(layout, Point{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.set_transform(shear);
    actual.draw_text(layout, Point{8.0f, 36.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 40.0f}, 1.0f);
    // the recorder expands the run through the engine resolver, which is what a composition root installs (§6.4, AD-34)
    SceneRecorder recorder(metrics);
    recorder.set_outline_resolver(engine->glyph_outline_resolver());
    recorder.set_background(Color::rgb(1.0f, 1.0f, 1.0f));
    recorder.draw_text(layout, Point{4.0f, 6.0f}, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 64.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[3] = {Point{12.0f, 8.0f}, Point{36.0f, 14.0f}, Point{18.0f, 26.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(91u, verbs, points);
    const Transform shear{.skew_y = 0.9f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, shear, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{28.0f, 28.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[17] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[16] = {Point{2.0f, 2.0f}, Point{22.0f, 2.0f}, Point{22.0f, 22.0f}, Point{2.0f, 22.0f}, Point{2.0f, 12.0f}, Point{18.0f, 12.0f}, Point{18.0f, 8.0f}, Point{2.0f, 8.0f}, Point{2.0f, 6.0f}, Point{4.0f, 6.0f}, Point{4.0f, 16.0f}, Point{20.0f, 16.0f}, Point{20.0f, 5.0f}, Point{6.0f, 5.0f}, Point{6.0f, 10.0f}, Point{2.0f, 10.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(93u, verbs, points);
    // act both backends fill the one path
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, Paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Point points[2] = {Point{10.0f, 24.0f}, Point{30.0f, 24.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(92u, points);
    const StrokeStyle stroke{.color = Color::rgb(0.2f, 0.2f, 0.2f), .width = 3.0f};
    const Transform turn = Transform::translation(-32.0f, -24.0f).then(Transform{.scale_x = 0.8253356f, .skew_x = -0.5646425f, .skew_y = 0.5646425f, .scale_y = 0.8253356f}).then(Transform::translation(32.0f, 24.0f));
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_line_batch(batch, turn, stroke);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_line_batch(batch, turn, stroke);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the segment lands where the turn maps its midpoint
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const Point mapped = turn.apply(Point{.x = 20.0f, .y = 24.0f});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 18.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(0.3f, 0.6f, 0.9f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(93u, commands);
    const Transform turn = Transform::translation(-24.0f, -24.0f).then(Transform{.scale_x = 0.8253356f, .skew_x = -0.5646425f, .skew_y = 0.5646425f, .scale_y = 0.8253356f}).then(Transform::translation(24.0f, 24.0f));
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_picture(picture, turn);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_picture(picture, turn);
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the picture centre lands where the turn maps it
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    const Point mapped = turn.apply(Point{.x = 9.0f, .y = 23.0f});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // act the first rectangle draws at half opacity, and the second at full once the state is popped
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.set_opacity(0.5f);
    expected.draw_rect(Rect::from_origin_size(Point{2.0f, 2.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected.restore();
    expected.draw_rect(Rect::from_origin_size(Point{18.0f, 2.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.set_opacity(0.5f);
    actual.draw_rect(Rect::from_origin_size(Point{2.0f, 2.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    actual.restore();
    actual.draw_rect(Rect::from_origin_size(Point{18.0f, 2.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[10] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[8] = {Point{6.0f, 6.0f}, Point{6.0f, 42.0f}, Point{42.0f, 42.0f}, Point{42.0f, 6.0f}, Point{16.0f, 16.0f}, Point{16.0f, 32.0f}, Point{32.0f, 32.0f}, Point{32.0f, 16.0f}};
    const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(94u, verbs, points);
    const Paint paint{.fill = Color::rgb(0.1f, 0.2f, 0.8f), .fill_rule = FillRule::EvenOdd};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_path(path, Transform{}, paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
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
        expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        expected.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        expected.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = paint.fill, .composition = mode});
        expected_surface->end_frame();
        RenderContext& actual = actual_surface->begin_frame();
        actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        actual.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        actual.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = paint.fill, .composition = mode});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const CompositionMode modes[] = {CompositionMode::SourceIn, CompositionMode::DestinationAtop, CompositionMode::Copy};
    const Paint plate{.fill = Color{.red = 0.8f, .green = 0.2f, .blue = 0.2f, .alpha = 0.6f}};
    const Paint overlay{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f, .alpha = 0.5f}};
    const Rect whole = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Rect whole = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect red = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Rect whole = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect plate = Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f});
    const Shadow cast{.color = Color::rgb(0.0f, 0.0f, 0.0f), .offset = Point{2.0f, 2.0f}, .blur = 0.0f};
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Rect whole = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect first = Rect::from_origin_size(Point{4.0f, 4.0f}, Size{24.0f, 24.0f});
    const Rect second = Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
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
        expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        expected.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        expected.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.2f, 0.2f, 0.8f), .composition = mode});
        expected_surface->end_frame();
        RenderContext& actual = actual_surface->begin_frame();
        actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        actual.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
        actual.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.2f, 0.2f, 0.8f), .composition = mode});
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
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Paint paint{.fill = Color{.red = 0.2f, .green = 0.2f, .blue = 0.8f, .alpha = 0.5f}, .composition = CompositionMode::DestinationIn};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{40.0f, 40.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    expected.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), paint);
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_rect(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{40.0f, 40.0f}), Paint{.fill = Color::rgb(0.8f, 0.2f, 0.2f)});
    actual.draw_rect(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), paint);
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
TEST(AlborRendererBackendChecks, RefusesASurfaceWithoutAValidExtent) {
    // arrange
    AlborRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, WindowMetrics{});
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(AlborRendererBackendChecks, DrawsTwoRangesOverOneVertexBuffer) {
    // arrange two overlapping rectangles, whose index ranges and uniforms differ per draw
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand first = DrawCommand::rectangle(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    first.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(first));
    DrawCommand second = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    second.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
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
    EXPECT_EQ(pixels[centre + 2u], 255u);
}

TEST(AlborRendererBackendChecks, DefersAResizeThatArrivesMidFrame) {
    // arrange
    AlborRendererBackend backend;
    const WindowMetrics small = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const WindowMetrics large = WindowMetrics::from_logical(Size{8.0f, 6.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, small);
    ASSERT_NE(surface, nullptr);
    // act
    RenderContext& context = surface->begin_frame();
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, small.logical_size), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    surface->resize(large);
    const WindowMetrics during = surface->metrics();
    surface->end_frame();
    // assert the deferred resize reached the target, which the next frame draws into (§6.2)
    EXPECT_EQ(during, small);
    EXPECT_EQ(surface->metrics(), large);
    const FrameScene scene(large, Color::rgb(0.0f, 0.0f, 1.0f));
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), 8u * 6u * 4u);
    EXPECT_EQ(pixels[2], 255u);
}

TEST(AlborRendererBackendChecks, RefusesAStaleSceneRatherThanRepairingIt) {
    // arrange an offscreen surface at one extent and a scene recorded at another (§6.1, D9.1)
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 24.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const FrameScene stale(WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f), Color::rgb(1.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(*surface, stale);
    // assert the frame was refused rather than repaired, so no frame finished at the stale extent (§6.1, D9.1)
    EXPECT_TRUE(surface->read_pixels().empty());
    EXPECT_EQ(surface->metrics(), metrics);
}

TEST(AlborRendererBackendChecks, PresentsThroughAnOffScreenWindow) {
    // arrange an off screen window, which is the handle a composition root hands over (§5.3)
    struct HiddenWindow
    {
        HWND window = nullptr;

        explicit HiddenWindow(const wchar_t* name) {
            // the class outlives the test, so a second run in one process accepts the one already registered (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = name;
            // the window is shown off screen, because a window the desktop composes keeps draining the swapchain (§6.3)
            if (RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"albor", WS_POPUP, -30000, -30000, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
                if (window != nullptr)
                    ShowWindow(window, SW_SHOWNOACTIVATE);
            }
        }

        ~HiddenWindow() {
            // the window is released after the surface that presents into it, which the declaration order gives (§5.3)
            if (window != nullptr)
                DestroyWindow(window);
        }
    };
    HiddenWindow host(L"albor-d3d-present");
    if (host.window == nullptr)
        GTEST_SKIP() << "no window";
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window)), metrics);
    // assert a host without a compositor skips, because it has no windowed path to present (§5.3)
    if (surface == nullptr)
        GTEST_SKIP() << "no composited window surface on this host";
    ASSERT_TRUE(surface->valid());
    // act a frame that copies the target into the swapchain and submits it (§6.3)
    const std::uint64_t before = d3d_presented_frames();
    const FrameScene scene(metrics, Color::rgb(0.0f, 1.0f, 0.0f));
    backend.execute_frame(*surface, scene);
    // assert the frame reached the swapchain as exactly one submit, which a skipped copy or submit would not (§6.3)
    EXPECT_EQ(d3d_presented_frames(), before + 1);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the frame drew at the window's extent, which a refused surface never reaches (§6.3)
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[1], 255u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 255u);
    // act an empty frame, which still copies and submits rather than skipping both (§6.1)
    surface->begin_frame();
    surface->end_frame();
    // assert the frame with no draw at all still reached the window, which a skipped submit would not (§6.1)
    EXPECT_EQ(d3d_presented_frames(), before + 2);
    const std::vector<std::uint8_t> empty = surface->read_pixels();
    // assert the frame with no draw at all left the transparent clear, which the copy carried (§6.1)
    ASSERT_EQ(empty.size(), 16u * 16u * 4u);
    EXPECT_EQ(empty[3], 0u);
    // act a resize after the presents, which only succeeds once no back buffer reference outlives a frame (§6.3)
    const WindowMetrics resized = WindowMetrics::from_logical(Size{20.0f, 16.0f}, 1.0f);
    backend.resize_surface(*surface, resized);
    // assert the buffers moved, because a leaked GetBuffer reference would make ResizeBuffers refuse (§6.3)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), resized);
    backend.execute_frame(*surface, FrameScene(resized, Color::rgb(0.0f, 0.0f, 1.0f)));
    EXPECT_EQ(surface->read_pixels().size(), 20u * 16u * 4u);
    // assert the resized window presents too, so the submit follows the buffers rather than the old extent (§6.3)
    EXPECT_EQ(d3d_presented_frames(), before + 3);
}

TEST(AlborRendererBackendChecks, ASurfaceKeepsItsWindowWhenAnotherBindsIt) {
    // arrange an off screen window, which two surfaces bind in turn (§5.3)
    struct HiddenWindow
    {
        HWND window = nullptr;

        explicit HiddenWindow(const wchar_t* name) {
            // the class outlives the test, so a second run in one process accepts the one already registered (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = name;
            // the window is shown off screen, because a window the desktop composes keeps draining the swapchain (§6.3)
            if (RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"albor", WS_POPUP, -30000, -30000, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
                if (window != nullptr)
                    ShowWindow(window, SW_SHOWNOACTIVATE);
            }
        }

        ~HiddenWindow() {
            // the window is released after the surfaces that bind it, which the declaration order gives (§5.3)
            if (window != nullptr)
                DestroyWindow(window);
        }
    };
    HiddenWindow host(L"albor-d3d-shared");
    if (host.window == nullptr)
        GTEST_SKIP() << "no window";
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const NativeWindowHandle handle = NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window));
    std::unique_ptr<RenderSurface> first = backend.create_surface(handle, metrics);
    // assert a host without a compositor skips, because it has no windowed path to bind (§5.3)
    if (first == nullptr)
        GTEST_SKIP() << "no composited window surface on this host";
    ASSERT_TRUE(first->valid());
    // act a second surface on the same window, which re-roots the visual tree (§5.3)
    std::unique_ptr<RenderSurface> second = backend.create_surface(handle, metrics);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(second->valid());
    // assert the first surface still draws and reads back while the second owns the window (§5.3)
    const FrameScene red(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    backend.execute_frame(*first, red);
    const std::vector<std::uint8_t> pixels = first->read_pixels();
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    EXPECT_EQ(pixels[0], 255u);
    EXPECT_EQ(pixels[2], 0u);
    // act the second surface leaving, which re-roots the window on the first (§5.3)
    second.reset();
    EXPECT_TRUE(first->valid());
    backend.execute_frame(*first, red);
    EXPECT_FALSE(first->read_pixels().empty());
    // act both leaving and a third surface taking the same handle, which a stale target would poison (§6.2)
    first.reset();
    std::unique_ptr<RenderSurface> third = backend.create_surface(handle, metrics);
    ASSERT_NE(third, nullptr);
    ASSERT_TRUE(third->valid());
    backend.execute_frame(*third, red);
    EXPECT_EQ(third->read_pixels().size(), 8u * 8u * 4u);
}

TEST(AlborRendererBackendChecks, AStreamOfPresentedFramesKeepsTheFootprintFlat) {
    // arrange an off screen window, which presents a real stream without showing it (§5.3)
    struct HiddenWindow
    {
        HWND window = nullptr;

        explicit HiddenWindow(const wchar_t* name) {
            // the class outlives the test, so a second run in one process accepts the one already registered (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = name;
            // the window is shown off screen, because a window the desktop composes keeps draining the swapchain (§6.3)
            if (RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"albor", WS_POPUP, -30000, -30000, 800, 600, nullptr, nullptr, window_class.hInstance, nullptr);
                if (window != nullptr)
                    ShowWindow(window, SW_SHOWNOACTIVATE);
            }
        }

        ~HiddenWindow() {
            // the window is released after the surface that presents into it, which the declaration order gives (§5.3)
            if (window != nullptr)
                DestroyWindow(window);
        }
    };
    HiddenWindow host(L"albor-d3d-stream");
    if (host.window == nullptr)
        GTEST_SKIP() << "no window";
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window)), metrics);
    // assert a host without a compositor skips, because it presents nothing to measure (§6.2)
    if (surface == nullptr)
        GTEST_SKIP() << "no composited window surface on this host";
    ASSERT_TRUE(surface->valid());
    // arrange a scene with one layer and one image, so every frame allocates an offscreen target and uploads (§6.2)
    FrameScene built(metrics, Color::rgb(0.06f, 0.07f, 0.09f));
    built.add_command(DrawCommand::layer_push(0.5f));
    const std::vector<std::uint8_t> source{200u, 120u, 60u, 255u};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(77u, 1u, 1u, ImageFormat::RGBA8, source);
    DrawCommand upload = DrawCommand::image(image, Rect::from_origin_size(Point{100.0f, 100.0f}, Size{64.0f, 64.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    upload.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    built.add_command(std::move(upload));
    built.add_command(DrawCommand::layer_pop());
    const std::shared_ptr<const FrameScene> scene = std::make_shared<const FrameScene>(std::move(built));
    RenderExecutor executor(&backend);
    // arrange the submit count the process starts from, which the stream has to raise once per frame (§6.1)
    const std::uint64_t presented = d3d_presented_frames();
    // act a warmup, so the measurement pays neither the target allocation nor the first swapchain buffers (§2.2)
    for (int frame = 0; frame < 20; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the private commit the stream settles at, which is the baseline the bound is judged against (§2.2)
    PROCESS_MEMORY_COUNTERS_EX before{};
    before.cb = sizeof(before);
    ASSERT_NE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before)), 0);
    // act the stream itself, which is a window that keeps presenting one unchanged scene (§6.1)
    for (int frame = 0; frame < 120; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the private commit the stream left behind, which one leaked target per frame lifts by a gigabyte (§2.2)
    PROCESS_MEMORY_COUNTERS_EX after{};
    after.cb = sizeof(after);
    ASSERT_NE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after)), 0);
    // assert every frame ran, because a stream that dropped them would hold the bound by drawing nothing (§6.1)
    EXPECT_EQ(executor.executed_count(), 140u);
    // assert every frame reached the window too, which a submit that stopped presenting would fail (§6.3)
    EXPECT_EQ(d3d_presented_frames(), presented + 140u);
    // assert the growth stays under two frame targets, which tolerates one target the driver retires late (§6.3)
    const std::int64_t frame_bytes = static_cast<std::int64_t>(metrics.pixel_size.width) * static_cast<std::int64_t>(metrics.pixel_size.height) * 4;
    const std::int64_t growth = static_cast<std::int64_t>(after.PrivateUsage) - static_cast<std::int64_t>(before.PrivateUsage);
    EXPECT_LT(growth, 2 * frame_bytes);
    // act a resize after the stream, which only succeeds once no back buffer reference is outstanding (§6.3)
    backend.resize_surface(*surface, WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f));
    // assert the buffers moved, because an outstanding GetBuffer reference makes ResizeBuffers fail (§6.3)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f));
}

TEST(AlborRendererBackendChecks, ReadsTheSameFinishedFrameTwice) {
    // arrange a frame whose read back allocates a staging texture on every call (§6.3)
    AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(1.0f, 0.0f, 0.0f)));
    // act
    const std::vector<std::uint8_t> first = surface->read_pixels();
    const std::vector<std::uint8_t> second = surface->read_pixels();
    // assert the second read sees the same finished frame, which a consumed frame or a reused mapping would not
    ASSERT_EQ(first.size(), 4u * 4u * 4u);
    ASSERT_EQ(second.size(), first.size());
    EXPECT_EQ(first, second);
    EXPECT_EQ(second[0], 255u);
}

TEST(AlborRendererBackendChecks, AppliesAResizeThatArrivedMidFrame) {
    // arrange an off screen window and a frame that is in flight (Â§5.3, D9.1)
    struct HiddenWindow
    {
        HWND window = nullptr;

        explicit HiddenWindow(const wchar_t* name) {
            // the class outlives the test, so a second run in one process accepts the one already registered (Â§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = name;
            // the window is shown off screen, because a window the desktop composes keeps draining the swapchain (Â§6.3)
            if (RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
                window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"albor", WS_POPUP, -30000, -30000, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
                if (window != nullptr)
                    ShowWindow(window, SW_SHOWNOACTIVATE);
            }
        }

        ~HiddenWindow() {
            // the window is released after the surface that presents into it, which the declaration order gives (Â§5.3)
            if (window != nullptr)
                DestroyWindow(window);
        }
    };
    HiddenWindow host(L"albor-d3d-deferred");
    if (host.window == nullptr)
        GTEST_SKIP() << "no window";
    AlborRendererBackend backend;
    const WindowMetrics small = WindowMetrics::from_logical(Size{16.0f, 12.0f}, 1.0f);
    const WindowMetrics large = WindowMetrics::from_logical(Size{32.0f, 24.0f}, 1.0f);
    std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window)), small);
    if (surface == nullptr)
        GTEST_SKIP() << "no composited window surface on this host";
    ASSERT_TRUE(surface->valid());
    // act a resize that arrives while the frame is in flight, which the surface defers to the end (Â§6.2)
    surface->begin_frame();
    backend.resize_surface(*surface, large);
    // assert the frame kept the extent it began at, because a mid frame resize must not split one frame (Â§6.2)
    EXPECT_EQ(surface->metrics(), small);
    surface->end_frame();
    // assert the deferred extent took, which a back buffer reference held across the resize would refuse (Â§6.3)
    EXPECT_EQ(surface->metrics(), large);
    EXPECT_TRUE(surface->valid());
    // act one frame at the deferred extent, which the swapchain buffers follow (Â§6.3)
    backend.execute_frame(*surface, FrameScene(large, Color::rgb(0.0f, 0.0f, 1.0f)));
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the frame drew at the new extent, so the resize reached the buffers and not only the metrics (Â§6.3)
    ASSERT_EQ(pixels.size(), 32u * 24u * 4u);
    EXPECT_EQ(pixels[2], 255u);
}
