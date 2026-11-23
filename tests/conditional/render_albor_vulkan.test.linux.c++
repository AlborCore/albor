#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/geometry.h"
#include "albor/core/paint.h"
#include "albor/render/draw_command.h"
#include "albor/render/frame_scene.h"
#include "albor/render/image_resource.h"
#include "albor/render/mesh_resource.h"
#include "albor/render/path_resource.h"
#include "albor/render/picture_resource.h"
#include "albor/render/render-albor/albor_glyph_path.h"
#include "albor/render/render-albor/albor_render_context.h"
#include "albor/render/render-albor/albor_renderer_backend.h"
#include "albor/render/render-albor/albor_shaders.h"
#include "albor/render/render-albor/albor_vulkan_diagnostics.h"
#include "albor/render/render-albor/albor_vulkan_support.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/render/render_context.h"
#include "albor/render/render_executor.h"
#include "albor/render/resource_factory.h"
#include "albor/render/screenshot.h"
#include "albor/render/shaped_run.h"
#include "albor/render/text_layout.h"
#include "albor/resources/embedded_font.h"
#include "albor/text/text-harfbuzz/harfbuzz_text_engine.h"
#include "albor/text/text_engine.h"
#include "albor/ui/scene_builder.h"
#include "albor/window/window_metrics.h"

using namespace albor;
using namespace albor::render;

TEST(AlborVulkanShaderChecks, CarriesTheCompiledSpirvContainer) {
    // arrange the bytes the build embedded, which are the stages the device loads (AD-16)
    const std::span<const std::byte> data = vulkan_shader_data();
    // act the container is parsed through the same path the device reads
    const VulkanShaderData parsed = parse_vulkan_shader_data(data);
    // assert the header and all six modules are present
    ASSERT_GT(data.size(), 8u);
    EXPECT_EQ(data[0], static_cast<std::byte>('A'));
    EXPECT_EQ(data[1], static_cast<std::byte>('L'));
    EXPECT_EQ(data[2], static_cast<std::byte>('B'));
    EXPECT_EQ(data[3], static_cast<std::byte>('V'));
    EXPECT_TRUE(parsed.complete());
}

TEST(AlborVulkanBackendChecks, BringsUpTheVulkanDevice) {
    // arrange
    AlborRendererBackend backend;
    // act
    const bool ready = backend.ready();
    // assert a linux host with a vulkan device can run the backend
    if (!ready)
        GTEST_SKIP() << "no vulkan device on this host";
    EXPECT_TRUE(ready);
}

TEST(AlborVulkanBackendChecks, RefusesASurfaceWithoutAValidExtent) {
    // arrange
    AlborRendererBackend backend;
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, WindowMetrics{});
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(AlborVulkanBackendChecks, BuildsAnOffscreenSurfaceWithoutAWindow) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 24.0f}, 2.0f);
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    // assert
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), metrics);
}

TEST(AlborVulkanBackendChecks, RasterizesABackgroundIntoTheOffscreenTarget) {
    // arrange an offscreen surface whose scene paints a red background
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the first and last pixel carry the background
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    EXPECT_EQ(pixels[0], 255u);
    EXPECT_EQ(pixels[1], 0u);
    EXPECT_EQ(pixels[2], 0u);
    EXPECT_EQ(pixels[3], 255u);
    const std::size_t last = pixels.size() - 4;
    EXPECT_EQ(pixels[last], 255u);
    EXPECT_EQ(pixels[last + 3], 255u);
}

TEST(AlborVulkanBackendChecks, RasterizesAnOpaqueRectangleOverTheBackground) {
    // arrange a blue rectangle on a white background
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the centre is blue and a corner stays white
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    const std::size_t centre = (8u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[centre], 0u);
    EXPECT_EQ(pixels[centre + 1u], 0u);
    EXPECT_EQ(pixels[centre + 2u], 255u);
    EXPECT_EQ(pixels[centre + 3u], 255u);
    const std::size_t corner = (1u * 16u + 1u) * 4u;
    EXPECT_EQ(pixels[corner], 255u);
    EXPECT_EQ(pixels[corner + 1u], 255u);
    EXPECT_EQ(pixels[corner + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, ReadsBackInCanonicalRgbaOrder) {
    // arrange a red background, which names red in the first byte only when the channels are canonical
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(1.0f, 0.0f, 0.0f)));
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert red is the first channel, which the read back swaps from the target's byte order
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    for (std::size_t index = 0; index < pixels.size(); index += 4u) {
        EXPECT_EQ(pixels[index], 255u);
        EXPECT_EQ(pixels[index + 1u], 0u);
        EXPECT_EQ(pixels[index + 2u], 0u);
        EXPECT_EQ(pixels[index + 3u], 255u);
    }
}

TEST(AlborVulkanBackendChecks, ReadsTheSameFinishedFrameTwice) {
    // arrange a surface that finished one frame
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.2f, 0.4f, 0.6f)));
    // act
    const std::vector<std::uint8_t> first = surface->read_pixels();
    const std::vector<std::uint8_t> second = surface->read_pixels();
    // assert the retained target answers both reads alike
    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(first, second);
}

TEST(AlborVulkanBackendChecks, APartialFramePreservesPixelsOutsideItsDamage) {
    // arrange a full frame that paints the whole target blue (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a partial frame that changes only the top left corner (§6.1)
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    green.set_clip(full_rect);
    partial_scene.add_command(std::move(green));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the damaged corner is green and the untouched corner remains the previous blue (§6.1)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    const std::size_t changed = (1u * 8u + 1u) * 4u;
    EXPECT_EQ(pixels[changed], 0u);
    EXPECT_EQ(pixels[changed + 1u], 255u);
    EXPECT_EQ(pixels[changed + 2u], 0u);
    const std::size_t untouched = (6u * 8u + 6u) * 4u;
    EXPECT_EQ(pixels[untouched], 0u);
    EXPECT_EQ(pixels[untouched + 1u], 0u);
    EXPECT_EQ(pixels[untouched + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, ResizesTheOffscreenTarget) {
    // arrange a surface at one extent, which draws one background
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics first = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, first);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(first, Color::rgb(0.0f, 0.0f, 1.0f)));
    // act
    const WindowMetrics second = WindowMetrics::from_logical(Size{12.0f, 10.0f}, 1.0f);
    backend.resize_surface(*surface, second);
    backend.execute_frame(*surface, FrameScene(second, Color::rgb(0.0f, 1.0f, 0.0f)));
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the target took the new extent and the new background
    ASSERT_EQ(pixels.size(), 12u * 10u * 4u);
    EXPECT_EQ(pixels[0], 0u);
    EXPECT_EQ(pixels[1], 255u);
    EXPECT_EQ(pixels[2], 0u);
}

TEST(AlborVulkanBackendChecks, ReusesTheBatchBuffersAcrossSteadyFrames) {
    // arrange a surface and one immutable scene, which every frame replays unchanged (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{24.0f, 16.0f}), Paint{.fill = Color::rgb(0.2f, 0.4f, 0.8f)}));
    // act a warmup that fills every retained buffer, which a steady frame then reuses (AD-06)
    for (int frame = 0; frame < 8; frame++)
        backend.execute_frame(*surface, scene);
    const std::uint64_t allocations = backend.batch_buffer_allocations();
    // assert the steady stream raised no further allocation
    ASSERT_GT(allocations, 0u);
    for (int frame = 0; frame < 32; frame++)
        backend.execute_frame(*surface, scene);
    EXPECT_EQ(backend.batch_buffer_allocations(), allocations);
}

TEST(AlborVulkanParityChecks, AnAlignedRectangleMatchesTheReference) {
    // arrange the same scene through the software reference and through this backend
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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
    // assert an aligned rectangle has no feathered edge to disagree on
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborVulkanParityChecks, AFeatheredEdgeMatchesTheReferenceWithinTolerance) {
    // arrange a rectangle whose edges land on fractional pixels
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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
    // assert the analytic edge agrees within the tolerance the reference leaves
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 32);
}

TEST(AlborVulkanParityChecks, ADoubleDensitySceneMatchesTheReference) {
    // arrange a scene at double density, which the device scale must carry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 2.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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
    // assert the physical extent and its flat interior agree exactly
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborVulkanParityChecks, ATranslucentImageMatchesTheReference) {
    // arrange a half transparent image, whose alpha the paint modulates
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the image is four opaque premultiplied colours, which the paint dims to half
    const std::uint8_t pixels[16] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u};
    ResourceFactory factory;
    const std::shared_ptr<const ImageResource> image = factory.create_image(2, 2, ImageFormat::RGBA8, pixels);
    ASSERT_NE(image, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert a scaled upload samples linearly in both backends, so the bound is the measured one plus margin
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 8);
}

TEST(AlborVulkanParityChecks, ASoftShadowMatchesTheReference) {
    // arrange a rounded card casting a soft shadow over a white background
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::shadow(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f}));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the cast darkens the surface under the card in both backends, which a missing shadow would leave white
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    const std::size_t under = (30u * 48u + 24u) * 4u;
    EXPECT_LT(actual.pixels[under], 250u);
    EXPECT_LT(expected.pixels[under], 250u);
    // the bound is the measured 46 plus margin, which a box and a gaussian blur reach at a soft edge
    EXPECT_LE(expected.max_channel_difference(actual), 64);
}

TEST(AlborVulkanParityChecks, EveryCompositionModeMatchesTheReference) {
    // arrange two overlapping rectangles, one drawn under each of the eleven modes
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{24.0f, 24.0f}, 1.0f);
    const CompositionMode modes[] = {CompositionMode::SourceOver, CompositionMode::SourceIn, CompositionMode::SourceOut, CompositionMode::Atop, CompositionMode::DestinationOver, CompositionMode::DestinationIn, CompositionMode::DestinationOut, CompositionMode::DestinationAtop, CompositionMode::Lighter, CompositionMode::Copy, CompositionMode::Xor};
    const Rect clip = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    for (CompositionMode mode : modes) {
        software::SoftwareRendererBackend reference;
        AlborRendererBackend backend;
        if (!backend.ready())
            GTEST_SKIP() << "no vulkan device on this host";
        const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
        const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
        ASSERT_NE(expected_surface, nullptr);
        ASSERT_NE(actual_surface, nullptr);
        FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
        DrawCommand under = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{12.0f, 12.0f}), Paint{.fill = Color::rgba(0.0f, 0.0f, 1.0f, 0.75f)});
        under.set_clip(clip);
        scene.add_command(std::move(under));
        DrawCommand over = DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{12.0f, 12.0f}), Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 0.75f), .composition = mode});
        over.set_clip(clip);
        scene.add_command(std::move(over));
        // act
        reference.execute_frame(*expected_surface, scene);
        backend.execute_frame(*actual_surface, scene);
        const Screenshot expected = capture_surface(*expected_surface);
        const Screenshot actual = capture_surface(*actual_surface);
        // assert the mode agrees within the tolerance of the feathered edges
        ASSERT_FALSE(expected.empty());
        ASSERT_FALSE(actual.empty());
        ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
        EXPECT_LE(expected.max_channel_difference(actual), 32) << "composition " << static_cast<int>(mode);
    }
}

TEST(AlborVulkanParityChecks, ADownscaledImageMatchesTheReference) {
    // arrange a checkerboard downscaled eight times, which a mip map keeps from aliasing
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    // the source is a 32 by 32 checkerboard of alternating opaque colours
    std::vector<std::uint8_t> source(32u * 32u * 4u, 0);
    for (std::size_t y = 0; y < 32; y++) {
        for (std::size_t x = 0; x < 32; x++) {
            // each cell alternates between the two colours, which a minified read must average
            const bool light = (x + y) % 2u == 0;
            const std::size_t at = (y * 32u + x) * 4u;
            source[at] = light ? 255u : 0u;
            source[at + 1u] = light ? 255u : 0u;
            source[at + 2u] = light ? 255u : 0u;
            source[at + 3u] = 255u;
        }
    }
    ResourceFactory factory;
    const std::shared_ptr<const ImageResource> image = factory.create_image(32, 32, ImageFormat::RGBA8, source);
    ASSERT_NE(image, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{2.0f, 2.0f}, Size{12.0f, 12.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the minified average is a mid grey, which a nearest tap would swing to black or white
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    const std::size_t interior = (8u * 16u + 8u) * 4u;
    EXPECT_GT(actual.pixels[interior], 60u);
    EXPECT_LT(actual.pixels[interior], 210u);
    // the bound is the measured 57 plus margin, which two mip filters reach at a hard edge
    EXPECT_LE(expected.max_channel_difference(actual), 72);
}

TEST(AlborVulkanParityChecks, AFilledPathMatchesTheReference) {
    // arrange a retained square path, which the reference fills through the same sweep
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[] = {Point{6.0f, 6.0f}, Point{26.0f, 8.0f}, Point{24.0f, 26.0f}, Point{8.0f, 24.0f}};
    ResourceFactory factory;
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, points);
    ASSERT_NE(path, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::path(path, Transform{}, Paint{.fill = Color::rgb(0.2f, 0.5f, 0.3f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the filled contour agrees within the feathered edge tolerance
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 32);
}

TEST(AlborVulkanParityChecks, AStrokedPolylineMatchesTheReference) {
    // arrange a stroked zigzag, whose caps and joins the expansion produces
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const Point points[] = {Point{4.0f, 24.0f}, Point{12.0f, 8.0f}, Point{20.0f, 24.0f}, Point{28.0f, 8.0f}};
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    DrawCommand command = DrawCommand::polyline(points, StrokeStyle{.color = Color::rgb(0.1f, 0.2f, 0.7f), .width = 3.0f});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the stroke agrees within the tolerance the feathered joins leave
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 40);
}

TEST(AlborVulkanParityChecks, AnEmbeddedShapedRunMatchesTheReference) {
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
    // act the one scene replays on both backends
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    reference.execute_frame(*expected_surface, *scene);
    backend.execute_frame(*actual_surface, *scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert both surfaces put ink in the run box, which an empty expansion would leave blank (§15)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    std::size_t expected_ink = 0;
    std::size_t actual_ink = 0;
    for (std::size_t index = 0; index < expected.pixels.size(); index += 4u) {
        if (expected.pixels[index] < 128u)
            expected_ink++;
        if (actual.pixels[index] < 128u)
            actual_ink++;
    }
    ASSERT_GT(expected_ink, 0u);
    EXPECT_LE(actual_ink, (expected_ink * 5u) / 4u);
    EXPECT_GE(actual_ink, (expected_ink * 3u) / 4u);
    // the same outlines fill on both, and the bound is the measured 57 plus margin
    EXPECT_LE(expected.max_channel_difference(actual), 64);
    // exit
    resources::FontRegistry::clear();
}

TEST(AlborVulkanParityChecks, TwoSoftShadowsMatchTheReference) {
    // arrange two casts in one frame, which each composite through their own quad range (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::shadow(Rect::from_origin_size(Point{6.0f, 8.0f}, Size{16.0f, 16.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 3.0f}, .blur = 6.0f}));
    scene.add_command(DrawCommand::shadow(Rect::from_origin_size(Point{38.0f, 20.0f}, Size{16.0f, 16.0f}), 4.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 3.0f}, .blur = 6.0f}));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert both casts landed where the reference put them
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    const std::size_t first = (20u * 64u + 14u) * 4u;
    const std::size_t second = (32u * 64u + 46u) * 4u;
    EXPECT_LT(actual.pixels[first], 250u);
    EXPECT_LT(actual.pixels[second], 250u);
    EXPECT_LE(expected.max_channel_difference(actual), 64);
}

TEST(AlborVulkanParityChecks, ADrawAfterASoftShadowStillLandsOnTheTarget) {
    // arrange a shadow and a rectangle after it, whose call-site invariant a starving budget would also starve
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::shadow(Rect::from_origin_size(Point{12.0f, 12.0f}, Size{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{0.0f, 4.0f}, .blur = 8.0f}));
    scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{2.0f, 2.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)}));
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the rectangle after the cast still reached the frame target
    ASSERT_EQ(pixels.size(), 48u * 48u * 4u);
    const std::size_t rectangle = (4u * 48u + 4u) * 4u;
    EXPECT_EQ(pixels[rectangle], 255u);
    EXPECT_EQ(pixels[rectangle + 1u], 0u);
    EXPECT_EQ(pixels[rectangle + 2u], 0u);
    EXPECT_EQ(pixels[rectangle + 3u], 255u);
}

TEST(AlborVulkanParityChecks, ANestedLayerMatchesTheReference) {
    // arrange a translucent layer around one rectangle, which composites once
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{24.0f, 24.0f}, 1.0f);
    software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::layer_push(0.5f));
    DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{16.0f, 16.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    scene.add_command(DrawCommand::layer_pop());
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the group dims once and agrees with the reference
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 32);
}
TEST(AlborVulkanBackendChecks, ADamageScopedFrameRendersATranslatedPicture) {
    // arrange a full frame that paints the whole target blue (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a picture whose local origin lies above the damage it is replayed inside (§6.4)
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(94u, recorder.commands());
    const Rect damage = Rect::from_origin_size(Point{0.0f, 4.0f}, Size{16.0f, 8.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand strip = DrawCommand::picture(picture, Transform::translation(0.0f, 4.0f));
    strip.set_clip(full_rect);
    partial_scene.add_command(std::move(strip));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the picture landed inside its damage, while the untouched row above keeps the previous blue (§6.1, §6.4)
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    const std::size_t changed = (7u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[changed], 255u);
    EXPECT_EQ(pixels[changed + 1u], 255u);
    EXPECT_EQ(pixels[changed + 2u], 255u);
    const std::size_t untouched = (1u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[untouched], 0u);
    EXPECT_EQ(pixels[untouched + 1u], 0u);
    EXPECT_EQ(pixels[untouched + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, ADamageScopedFrameMapsAPictureInnerClipThroughItsTranslation) {
    // arrange a full frame that paints the whole target blue (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a picture whose fill is clipped in its own space, which a tab label records (§6.2, §6.4)
    PictureRecorder recorder;
    recorder.save();
    recorder.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 4.0f}));
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    recorder.restore();
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(95u, recorder.commands());
    const Rect damage = Rect::from_origin_size(Point{0.0f, 4.0f}, Size{16.0f, 8.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand strip = DrawCommand::picture(picture, Transform::translation(0.0f, 4.0f));
    strip.set_clip(full_rect);
    partial_scene.add_command(std::move(strip));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the picture's inner clip kept its ink and cut the fill beyond it (§6.2, §6.4)
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    const std::size_t inside = (7u * 16u + 4u) * 4u;
    const std::size_t outside = (7u * 16u + 12u) * 4u;
    EXPECT_EQ(pixels[inside], 255u);
    EXPECT_EQ(pixels[inside + 1u], 255u);
    EXPECT_EQ(pixels[inside + 2u], 255u);
    EXPECT_EQ(pixels[outside], 0u);
    EXPECT_EQ(pixels[outside + 1u], 0u);
    EXPECT_EQ(pixels[outside + 2u], 0u);
}

TEST(AlborVulkanBackendChecks, ADamageScopedFrameScalesAPictureInsideItsDamage) {
    // arrange a full frame that paints the whole target blue (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a picture replayed at double scale inside the damage (§6.4)
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 4.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(96u, recorder.commands());
    const Rect damage = Rect::from_origin_size(Point{0.0f, 4.0f}, Size{16.0f, 8.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    const Transform placement = Transform::scaling(2.0f, 2.0f).then(Transform::translation(0.0f, 4.0f));
    DrawCommand strip = DrawCommand::picture(picture, placement);
    strip.set_clip(full_rect);
    partial_scene.add_command(std::move(strip));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the scaled picture landed inside its damage, while the row above keeps the previous blue (§6.1, §6.4)
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    const std::size_t changed = (7u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[changed], 255u);
    EXPECT_EQ(pixels[changed + 1u], 255u);
    EXPECT_EQ(pixels[changed + 2u], 255u);
    const std::size_t untouched = (1u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[untouched + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, ADamageScopedFrameClipsAPictureAtAContainerClip) {
    // arrange a full frame that paints the whole target blue (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a picture that crosses a container clip narrower than the damage (§6.2, §6.4)
    PictureRecorder recorder;
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(97u, recorder.commands());
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand strip = DrawCommand::picture(picture, Transform::translation(0.0f, 4.0f));
    strip.set_clip(Rect::from_origin_size(Point{0.0f, 8.0f}, Size{16.0f, 8.0f}));
    partial_scene.add_command(std::move(strip));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the container clip keeps its band and cuts the picture above it (§6.2)
    ASSERT_EQ(pixels.size(), 16u * 16u * 4u);
    const std::size_t inside = (10u * 16u + 8u) * 4u;
    const std::size_t above = (6u * 16u + 8u) * 4u;
    EXPECT_EQ(pixels[inside], 255u);
    EXPECT_EQ(pixels[inside + 1u], 255u);
    EXPECT_EQ(pixels[inside + 2u], 255u);
    EXPECT_EQ(pixels[above], 0u);
    EXPECT_EQ(pixels[above + 1u], 0u);
    EXPECT_EQ(pixels[above + 2u], 0u);
}

TEST(AlborVulkanBackendChecks, APartialFrameMapsItsDamageThroughTheDeviceScale) {
    // arrange a two-to-one surface, where logical damage and physical pixels differ (§6.1, AD-27)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 2.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{2.0f, 2.0f});
    FrameScene partial_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    green.set_clip(full_rect);
    partial_scene.add_command(std::move(green));
    // act
    backend.execute_frame(*surface, partial_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the logical damage covers the physical top left and the rest keeps the previous blue (§6.1)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    const std::size_t changed = (2u * 8u + 2u) * 4u;
    EXPECT_EQ(pixels[changed + 1u], 255u);
    const std::size_t untouched = (6u * 8u + 6u) * 4u;
    EXPECT_EQ(pixels[untouched + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, APartialFrameOnAFreshTargetFillsTheBackground) {
    // arrange a fresh target and a partial scene, which has no prior pixels to load (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    const FrameScene scene(metrics, Color::rgb(1.0f, 0.0f, 0.0f), damage);
    // act
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the refused partial hint made a full background frame, so no pixel is undefined (§6.1)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    for (std::size_t pixel = 0; pixel < 64u; pixel++) {
        EXPECT_EQ(pixels[pixel * 4u], 255u);
        EXPECT_EQ(pixels[pixel * 4u + 3u], 255u);
    }
}

TEST(AlborVulkanBackendChecks, APartialFrameWithATranslucentBackgroundIsAFullFrame) {
    // arrange a full translucent target, which a partial frame cannot reproduce with an opaque replacement (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Color background = Color::rgba(0.0f, 0.0f, 1.0f, 0.5f);
    backend.execute_frame(*surface, FrameScene(metrics, background));
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    const FrameScene partial(metrics, background, damage);
    // act
    backend.execute_frame(*surface, partial);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the alpha is the background alpha everywhere, because a translucent scene stays full (§6.1)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    for (std::size_t pixel = 0; pixel < 64u; pixel++)
        EXPECT_NEAR(pixels[pixel * 4u + 3u], 128u, 1u);
}

TEST(AlborVulkanBackendChecks, TheSurfaceKeepsItsTargetBetweenFrames) {
    // arrange a surface over a device, which is what stage one asks before it culls (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act, assert the private target is reported as retained, so a culled scene may load it (§6.1, §6.3)
    EXPECT_TRUE(surface->retains_target());
}

TEST(AlborVulkanBackendChecks, ACulledSceneOnAFreshTargetIsRefused) {
    // arrange a fresh target and a culled scene, which has no prior pixels to preserve (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage, true);
    DrawCommand green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    green.set_clip(full_rect);
    scene.add_command(std::move(green));
    // act
    const FrameOutcome outcome = backend.execute_frame(*surface, scene);
    // assert the surface refused, because a culled list on a fresh target would lose the rest of the frame (§6.1)
    EXPECT_EQ(outcome, FrameOutcome::RefusedNoRetention);
    // assert nothing was presented, so the window keeps the frame it already had (§6.1)
    EXPECT_TRUE(surface->read_pixels().empty());
}

TEST(AlborVulkanBackendChecks, ACulledSceneDrawsInsideItsDamage) {
    // arrange a full blue target, which is what a culled scene loads (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    // arrange a culled scene whose only command reaches the damage (§6.1)
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    FrameScene culled(metrics, Color::rgb(0.0f, 0.0f, 1.0f), damage, true);
    DrawCommand green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    green.set_clip(full_rect);
    culled.add_command(std::move(green));
    // act
    const FrameOutcome outcome = backend.execute_frame(*surface, culled);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the culled scene presented and only the damaged corner changed (§6.1)
    EXPECT_EQ(outcome, FrameOutcome::Presented);
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    const std::size_t changed = (1u * 8u + 1u) * 4u;
    EXPECT_EQ(pixels[changed], 0u);
    EXPECT_EQ(pixels[changed + 1u], 255u);
    const std::size_t untouched = (6u * 8u + 6u) * 4u;
    EXPECT_EQ(pixels[untouched + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, ACulledFrameMatchesTheWholeFrameOverTheSameDamage) {
    // arrange two targets that begin from the same whole frame (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> whole = backend.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> culled = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(whole, nullptr);
    ASSERT_NE(culled, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 4.0f});
    const Rect distant = Rect::from_origin_size(Point{6.0f, 6.0f}, Size{2.0f, 2.0f});
    FrameScene base(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand base_far = DrawCommand::rectangle(distant, Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    base_far.set_clip(full_rect);
    base.add_command(std::move(base_far));
    backend.execute_frame(*whole, base);
    backend.execute_frame(*culled, base);
    // arrange the same change twice, once with the whole list and once with the list culled (§6.1)
    FrameScene full_change(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage);
    DrawCommand full_green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    full_green.set_clip(full_rect);
    DrawCommand full_far = DrawCommand::rectangle(distant, Paint{.fill = Color::rgb(0.0f, 0.0f, 1.0f)});
    full_far.set_clip(full_rect);
    full_change.add_command(std::move(full_green));
    full_change.add_command(std::move(full_far));
    FrameScene culled_change(metrics, Color::rgb(0.0f, 0.0f, 0.0f), damage, true);
    DrawCommand culled_green = DrawCommand::rectangle(damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    culled_green.set_clip(full_rect);
    culled_change.add_command(std::move(culled_green));
    // act
    backend.execute_frame(*whole, full_change);
    backend.execute_frame(*culled, culled_change);
    // assert a list culled at stage one presents the same pixels as the whole list (§6.1)
    EXPECT_EQ(whole->read_pixels(), culled->read_pixels());
}

TEST(AlborVulkanBackendChecks, TwoPartialFramesKeepEachOthersPixels) {
    // arrange a full blue target and two adjacent deltas, which must both survive (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.0f, 0.0f, 1.0f)));
    const Rect first_damage = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{2.0f, 2.0f});
    FrameScene first_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), first_damage);
    DrawCommand green = DrawCommand::rectangle(first_damage, Paint{.fill = Color::rgb(0.0f, 1.0f, 0.0f)});
    green.set_clip(full_rect);
    first_scene.add_command(std::move(green));
    backend.execute_frame(*surface, first_scene);
    const Rect second_damage = Rect::from_origin_size(Point{2.0f, 2.0f}, Size{2.0f, 2.0f});
    FrameScene second_scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f), second_damage);
    DrawCommand red = DrawCommand::rectangle(second_damage, Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    red.set_clip(full_rect);
    second_scene.add_command(std::move(red));
    // act
    backend.execute_frame(*surface, second_scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the first delta kept its green and the second painted red over the blue base (§6.1)
    ASSERT_EQ(pixels.size(), 8u * 8u * 4u);
    const std::size_t first_pixel = (1u * 8u + 1u) * 4u;
    EXPECT_EQ(pixels[first_pixel + 1u], 255u);
    const std::size_t second_pixel = (3u * 8u + 3u) * 4u;
    EXPECT_EQ(pixels[second_pixel], 255u);
    const std::size_t base_pixel = (5u * 8u + 5u) * 4u;
    EXPECT_EQ(pixels[base_pixel + 2u], 255u);
}

TEST(AlborVulkanBackendChecks, RefusesAReadWhileAFrameIsInFlight) {
    // arrange a surface with one finished frame, so the refusal is the flight and not the first frame (§6.3)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, IgnoresAnEndWithoutABegin) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    surface->end_frame();
    // assert no frame finished, so the read reports nothing (§6.1)
    EXPECT_TRUE(surface->read_pixels().empty());
}

TEST(AlborVulkanBackendChecks, AntialiasesAFractionalEdge) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, CutsADrawAtTheClipItCarries) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, DimsADrawByItsOpacity) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, RefusesAResizeToAnEmptyExtent) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, KeepsTheTargetWhenResizedToAnEmptyExtent) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ATwoScaleSceneMatchesTheReference) {
    // arrange a scene at double density, which the device scale must carry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 2.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARoundedClipMatchesTheReferenceAtItsCorners) {
    // arrange a rounded clip driven through the contexts, because the recorder reduces a rounded clip to its rectangle
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ReusesAnUploadedTextureAcrossFrames) {
    // arrange the same retained image drawn on two consecutive frames
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{8.0f, 8.0f}, 1.0f);
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARectangularScissorMatchesTheReference) {
    // arrange a full surface fill cut by a clip at a fractional position, which the scissor mask decides
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AScissorUnderATranslatedTransformMatchesTheReference) {
    // arrange a clip pushed under a translated transform, which the frozen clip matrix must carry
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AFrameRenderedAtAnotherScaleDoublesTheGeometry) {
    // arrange the same logical square at two densities, which the device scale must carry into the geometry (AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    const WindowMetrics denser = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 2.0f);
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AHardShadowMatchesTheReferenceWithinTolerance) {
    // arrange a zero blur shadow, which is a hard offset copy of the caster
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARoundedRectangleMatchesTheReference) {
    // arrange a filled and stroked rounded rectangle, which exercises the arc tessellation
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AHoledPathMatchesTheReference) {
    // arrange a square path with an opposite wound inner square, which the reference cuts as a hole
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ALineBatchMatchesTheReference) {
    // arrange a retained batch of two segments, which the backend coalesces into one draw
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, APictureMatchesTheReference) {
    // arrange a retained picture replayed under a translation
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ATranslucentOverlapMatchesTheReference) {
    // arrange two translucent fills that overlap, which the blend order decides
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ASoftShadowOnAWideCasterMatchesTheReference) {
    // arrange a wide caster, whose vertical and horizontal blurs need different texel steps
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AShearedCasterMatchesTheReference) {
    // arrange a caster drawn under a shear, whose parallelogram the reference renders as it is
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AFractionalImageDestinationFeathersItsEdge) {
    // arrange an image whose destination lands on fractional pixels
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{24.0f, 24.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, CompositesACastOnAFrameWithNoGeometry) {
    // arrange a frame whose only content is a shadow, which no vertex range carries
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, KeepsAFrameThatUploadsMoreImagesThanTheCacheHolds) {
    // arrange a surface pair and seventy images, which is more than the texture cache is sized for (§6.4, AD-06)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 96.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, RedrawsATextureTheIdleSweepDropped) {
    // arrange a surface pair and one image, which the first frame uploads into the cache (§6.4, AD-06)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::vector<std::uint8_t> source{64u, 128u, 192u, 255u};
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(500u, 1u, 1u, ImageFormat::RGBA8, source);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::image(image, Rect::from_origin_size(Point{4.0f, 4.0f}, Size{8.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    // act the surface runs past the 120 frame idle limit, which drops the image and recycles its slot (§6.4, AD-06)
    FrameScene idle(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    for (std::uint32_t frame = 0u; frame < 130u; frame++)
        backend.execute_frame(*actual_surface, idle);
    // act the image is drawn again, which the sweep no longer answers and uploads a second time (AD-06)
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the re-uploaded image lands where the reference draws it, which a missing sweep answer would not (§6.4, AD-06)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(actual_shot.max_channel_difference(expected_shot), 2);
}

TEST(AlborVulkanParityChecks, AFilledConcavePathMatchesTheReference) {
    // arrange an L-shaped closed path, whose notch a fan cannot cover
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AShapedRunMatchesTheReference) {
    // arrange a platform run, which the reference rasterizes from the face and the backend fills from its outline
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Al", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    // the run is placed by the face's own ascent, so its glyphs stay inside the surface on every platform face (§9.1)
    const Point run_origin{.x = 2.0f, .y = (metrics.logical_size.height - (layout.ascent + layout.descent)) * 0.5f};
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.draw_text(layout, run_origin, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.draw_text(layout, run_origin, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both surfaces put ink down, because a zero ink reference makes every comparison meaningless (§6.3)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
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
    ASSERT_GT(actual_ink, 0u);
    // the bound is the 70 measured on this host, which a hinted stem drawn unhinted leaves on its edges
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 80);
    // the atlas and the outline rasterizer cover a comparable area, which a displaced baseline breaks
    EXPECT_LE(actual_ink, expected_ink * 2u);
}

TEST(AlborVulkanParityChecks, ARotatedShapedRunMatchesTheReference) {
    // arrange a platform run turned a fifth of a turn about its own baseline origin (§9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::unique_ptr<albor::harfbuzz::HarfbuzzTextEngine> engine = albor::harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Al", 0.0f, 28.0f);
    ASSERT_FALSE(layout.empty());
    // the run is placed by the face's own ascent, so its turned glyphs stay inside the surface (§9.1)
    const Point run_origin{.x = 8.0f, .y = (metrics.logical_size.height - (layout.ascent + layout.descent)) * 0.5f};
    // the turn pivots on the run's baseline origin, so the transform rotates it in place rather than moving it away
    const Point pivot{.x = run_origin.x, .y = run_origin.y + layout.ascent};
    const Transform turn = Transform::translation(-pivot.x, -pivot.y).then(Transform{.scale_x = 0.9396926f, .skew_x = -0.34202015f, .skew_y = 0.34202015f, .scale_y = 0.9396926f}).then(Transform::translation(pivot.x, pivot.y));
    // act the same transform reaches both contexts, which the backends must map the same way (§6.3)
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.set_transform(turn);
    expected.draw_text(layout, run_origin, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.set_transform(turn);
    actual.draw_text(layout, run_origin, Paint{.fill = Color::rgb(0.1f, 0.1f, 0.1f)});
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert both turned runs drew ink, which a run left outside the surface would not (§9.1)
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
    ASSERT_GT(actual_ink, 0u);
    EXPECT_LE(actual_ink, (expected_ink * 5u) / 4u);
    // the ink spans the same columns, which a transform mapped in the wrong order would displace
    EXPECT_NEAR(static_cast<double>(actual_left), static_cast<double>(expected_left), 2.0);
    EXPECT_NEAR(static_cast<double>(actual_right), static_cast<double>(expected_right), 2.0);
    // the bound is the 130 measured on this host, which a turned outline leaves on its edges (§9.1)
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 160);
}

TEST(AlborVulkanParityChecks, AShearedShapedRunMatchesTheReference) {
    // arrange a platform run sheared about its baseline origin, which tilts every glyph column (§9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AShearedRetainedPathMatchesTheReference) {
    // arrange a retained contour under a shear, which the reference maps into the parallelogram it names (§6.4, §6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 64.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ASelfOverlappingPathCutsItsCounterOnBothBackends) {
    // arrange one contour whose bar is crossed by its own stroke, which leaves the pocket below the bar at winding zero
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{28.0f, 28.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARotatedLineBatchMatchesTheReference) {
    // arrange a retained batch turned 0.6 radians about the surface centre, which a translation cannot do (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARotatedPictureMatchesTheReference) {
    // arrange a retained picture replayed a quarter turn about the surface centre (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ARotatedPictureInsideAContainerClipMatchesTheReference) {
    // arrange a retained picture turned about the surface centre inside a narrow container clip (§6.2, §6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(Rect::from_origin_size(Point{6.0f, 20.0f}, Size{36.0f, 8.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(98u, commands);
    const Transform turn = Transform::translation(-24.0f, -24.0f).then(Transform{.scale_x = 0.70710678f, .skew_x = -0.70710678f, .skew_y = 0.70710678f, .scale_y = 0.70710678f}).then(Transform::translation(24.0f, 24.0f));
    const Rect full_rect = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
    const Rect container = Rect::from_origin_size(Point{20.0f, 20.0f}, Size{8.0f, 8.0f});
    // act
    RenderContext& expected = expected_surface->begin_frame();
    expected.draw_rect(full_rect, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    expected.save();
    expected.clip_rect(container);
    expected.draw_picture(picture, turn);
    expected.restore();
    expected_surface->end_frame();
    RenderContext& actual = actual_surface->begin_frame();
    actual.draw_rect(full_rect, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    actual.save();
    actual.clip_rect(container);
    actual.draw_picture(picture, turn);
    actual.restore();
    actual_surface->end_frame();
    const Screenshot expected_shot = capture_surface(*expected_surface);
    const Screenshot actual_shot = capture_surface(*actual_surface);
    // assert the container clip survives the rotated placement on both backends (§6.2, AD-26)
    ASSERT_FALSE(expected_shot.empty());
    ASSERT_FALSE(actual_shot.empty());
    ASSERT_EQ(expected_shot.pixels.size(), actual_shot.pixels.size());
    EXPECT_LE(expected_shot.max_channel_difference(actual_shot), 32);
}

TEST(AlborVulkanParityChecks, AContextOpacityDimsThePixels) {
    // arrange a context opacity around one draw, which the reference dims while it composites (§6.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, AnEvenOddPathMatchesTheReference) {
    // arrange two nested squares of one winding, whose hole only the even odd rule cuts (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ATranslucentSourceFollowsEveryMode) {
    // arrange one half transparent overlay per composition mode over an opaque plate (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ATranslucentSourceFollowsEveryModeOverATranslucentDestination) {
    // arrange a half transparent plate inside a layer, which leaves a destination alpha to blend against (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ALayerAroundACastOnlyFrameMatchesTheReference) {
    // arrange a half opaque group whose only content is a hard cast, so the frame carries no geometry (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ALayerDimsItsGroupOnce) {
    // arrange a half opaque layer over a white plate, whose two red children overlap by design (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ATranslucentDestinationInMatchesTheReference) {
    // arrange a half transparent overlay that keeps the destination it covers, which reshapes the residual (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{48.0f, 48.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, DrawsTwoRangesOverOneVertexBuffer) {
    // arrange two overlapping rectangles, whose index ranges and uniforms differ per draw
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, DefersAResizeThatArrivesMidFrame) {
    // arrange
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanBackendChecks, RefusesAStaleSceneRatherThanRepairingIt) {
    // arrange an offscreen surface at one extent and a scene recorded at another (§6.1, D9.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
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

TEST(AlborVulkanParityChecks, ASolidVertexColourQuadMatchesTheReferenceExactly) {
    // arrange a red quad covering the whole target, which both engines must fill identically (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const MeshVertex vertices[4] = {{.position = Point{0.0f, 0.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{16.0f, 0.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{0.0f, 16.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{16.0f, 16.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}};
    const std::uint32_t indices[6] = {0u, 1u, 3u, 0u, 3u, 2u};
    const std::shared_ptr<const MeshResource> mesh = std::make_shared<const MeshResource>(1101u, vertices, indices);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::mesh(mesh, nullptr, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the whole target agrees, because the quad covers every pixel centre (§6.4, AD-26)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_EQ(expected.max_channel_difference(actual), 0);
}

TEST(AlborVulkanParityChecks, ATexturedMeshQuadMatchesTheReference) {
    // arrange a two by two texture on a two by two quad, which samples one texel per pixel (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const std::uint8_t pixels[16] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u};
    const std::shared_ptr<const ImageResource> texture = std::make_shared<const ImageResource>(1102u, 2u, 2u, ImageFormat::RGBA8, pixels);
    const MeshVertex vertices[4] = {{.position = Point{4.0f, 4.0f}, .uv = Point{0.0f, 0.0f}}, {.position = Point{6.0f, 4.0f}, .uv = Point{1.0f, 0.0f}}, {.position = Point{4.0f, 6.0f}, .uv = Point{0.0f, 1.0f}}, {.position = Point{6.0f, 6.0f}, .uv = Point{1.0f, 1.0f}}};
    const std::uint32_t indices[6] = {0u, 1u, 2u, 2u, 1u, 3u};
    const std::shared_ptr<const MeshResource> mesh = std::make_shared<const MeshResource>(1103u, vertices, indices);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::mesh(mesh, texture, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the two samplers read the same four texels, within the upload's byte rounding (§6.4, AD-26)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 1);
}

TEST(AlborVulkanParityChecks, APerVertexColourMeshMatchesTheReference) {
    // arrange a four colour quad, whose corners both engines interpolate across two triangles (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const MeshVertex vertices[4] = {{.position = Point{4.0f, 4.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{12.0f, 4.0f}, .color = Color::rgb(0.0f, 1.0f, 0.0f)}, {.position = Point{4.0f, 12.0f}, .color = Color::rgb(0.0f, 0.0f, 1.0f)}, {.position = Point{12.0f, 12.0f}, .color = Color::rgb(1.0f, 1.0f, 1.0f)}};
    const std::uint32_t indices[6] = {0u, 1u, 3u, 0u, 2u, 3u};
    const std::shared_ptr<const MeshResource> mesh = std::make_shared<const MeshResource>(1104u, vertices, indices);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::mesh(mesh, nullptr, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the interpolated colours agree, within the one byte the two interpolators round apart (§6.4)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 2);
}

TEST(AlborVulkanParityChecks, APlacedAndClippedMeshMatchesTheReference) {
    // arrange a mesh under a scaled translation and a clip, which both engines place alike (§6.4, AD-27)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const MeshVertex vertices[4] = {{.position = Point{0.0f, 0.0f}, .color = Color::rgb(0.0f, 1.0f, 0.0f)}, {.position = Point{8.0f, 0.0f}, .color = Color::rgb(0.0f, 1.0f, 0.0f)}, {.position = Point{0.0f, 8.0f}, .color = Color::rgb(0.0f, 1.0f, 0.0f)}, {.position = Point{8.0f, 8.0f}, .color = Color::rgb(0.0f, 1.0f, 0.0f)}};
    const std::uint32_t indices[6] = {0u, 1u, 3u, 0u, 3u, 2u};
    const std::shared_ptr<const MeshResource> mesh = std::make_shared<const MeshResource>(1105u, vertices, indices);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::mesh(mesh, nullptr, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    command.set_transform(Transform{.scale_x = 1.5f, .translate_x = 1.0f, .scale_y = 1.5f, .translate_y = 2.0f});
    command.set_clip(Rect::from_origin_size(Point{2.0f, 2.0f}, Size{12.0f, 12.0f}));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the placed and clipped mesh agrees, within one byte at the clip edge (§6.2, §6.4)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 2);
}

TEST(AlborVulkanParityChecks, AMeshCopyCompositionMatchesTheReference) {
    // arrange a blue target and a red mesh drawn with copy, which must replace only its own pixels (§6.2)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    const MeshVertex vertices[4] = {{.position = Point{4.0f, 4.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{12.0f, 4.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{4.0f, 12.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{12.0f, 12.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}};
    const std::uint32_t indices[6] = {0u, 1u, 3u, 0u, 3u, 2u};
    const std::shared_ptr<const MeshResource> mesh = std::make_shared<const MeshResource>(1106u, vertices, indices);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 1.0f));
    DrawCommand command = DrawCommand::mesh(mesh, nullptr, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f), .composition = CompositionMode::Copy});
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the copy stayed inside the mesh, within one byte at its edge (§6.2)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 1);
}

TEST(AlborVulkanParityChecks, APictureMeshMatchesTheReference) {
    // arrange a picture of one red quad, placed under a translation (§6.4)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{16.0f, 16.0f}, 1.0f);
    albor::software::SoftwareRendererBackend reference;
    albor::render::AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> expected_surface = reference.create_surface(NativeWindowHandle{}, metrics);
    const std::unique_ptr<RenderSurface> actual_surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(expected_surface, nullptr);
    ASSERT_NE(actual_surface, nullptr);
    ResourceFactory factory;
    const MeshVertex vertices[4] = {{.position = Point{0.0f, 0.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{8.0f, 0.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{0.0f, 8.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}, {.position = Point{8.0f, 8.0f}, .color = Color::rgb(1.0f, 0.0f, 0.0f)}};
    const std::uint32_t indices[6] = {0u, 1u, 3u, 0u, 3u, 2u};
    const std::shared_ptr<const MeshResource> mesh = factory.create_mesh(vertices, indices);
    ASSERT_NE(mesh, nullptr);
    PictureRecorder recorder;
    recorder.draw_mesh(mesh, nullptr, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(recorder);
    ASSERT_NE(picture, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    DrawCommand command = DrawCommand::picture(picture, Transform::translation(4.0f, 4.0f));
    command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    scene.add_command(std::move(command));
    // act
    reference.execute_frame(*expected_surface, scene);
    backend.execute_frame(*actual_surface, scene);
    const Screenshot expected = capture_surface(*expected_surface);
    const Screenshot actual = capture_surface(*actual_surface);
    // assert the mesh inside the picture landed at its placed position, within one byte (§6.4)
    ASSERT_FALSE(expected.empty());
    ASSERT_FALSE(actual.empty());
    ASSERT_EQ(expected.pixels.size(), actual.pixels.size());
    EXPECT_LE(expected.max_channel_difference(actual), 1);
}

TEST(AlborVulkanBackendChecks, ZeroAllocationsDuringSteadyStateAnimation) {
    // arrange a surface that already uploaded its textures and grew its scratch targets (§6.3, AD-06)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    auto animated = [&metrics](int frame) {
        FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
        const float offset = static_cast<float>(frame % 8);
        scene.add_command(DrawCommand::shadow(Rect::from_origin_size(Point{8.0f + offset, 8.0f}, Size{24.0f, 24.0f}), 6.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.6f), .offset = Point{offset, 4.0f}, .blur = 8.0f}));
        scene.add_command(DrawCommand::layer_push(0.75f));
        scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{6.0f + offset, 6.0f}, Size{12.0f, 12.0f}), Paint{.fill = Color::rgb(0.2f, 0.4f, 0.8f)}));
        scene.add_command(DrawCommand::layer_pop());
        return scene;
    };
    // act the first frames build every retained buffer and scratch target the scene needs (§6.3, AD-06)
    for (int frame = 0; frame < 4; frame++)
        backend.execute_frame(*surface, animated(frame));
    const std::uint64_t allocations = vulkan_device_allocations();
    // assert a steady animation allocates no further device memory, which a per frame target would raise (AD-06)
    for (int frame = 0; frame < 32; frame++)
        backend.execute_frame(*surface, animated(frame));
    EXPECT_EQ(vulkan_device_allocations(), allocations);
}

TEST(AlborVulkanBackendChecks, ServesMoreDrawsThanTheDescriptorPoolWasSizedFor) {
    // arrange a scene with ten thousand draws, which outgrows the ring and the pool a frame starts with (§6.3)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{32.0f, 32.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    FrameScene scene(metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    for (int draw = 0; draw < 10000; draw++) {
        const float x = static_cast<float>(draw % 32);
        const float y = static_cast<float>((draw / 32) % 32);
        DrawCommand command = DrawCommand::rectangle(Rect::from_origin_size(Point{x, y}, Size{1.0f, 1.0f}), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
        command.set_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        scene.add_command(std::move(command));
    }
    // act the one frame replays every draw, which a pool or ring that never grew would drop (§6.3)
    backend.execute_frame(*surface, scene);
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the last draw landed, which a frame that stopped at the pool ceiling would leave black (§6.3)
    ASSERT_EQ(pixels.size(), 32u * 32u * 4u);
    std::size_t painted = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 4u) {
        if (pixels[index] == 255u)
            painted++;
    }
    EXPECT_GT(painted, 900u);
}

TEST(AlborVulkanBackendChecks, ClearsAnEmptyOffscreenFrameToTransparent) {
    // arrange an offscreen surface with a frame that records nothing (§6.1)
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{4.0f, 4.0f}, 1.0f);
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    // act
    const std::uint64_t before = vulkan_presented_frames();
    surface->begin_frame();
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert an offscreen frame presents nothing, which the process counter shows (§6.3)
    EXPECT_EQ(vulkan_presented_frames(), before);
    // assert every pixel is the transparent clear a frame with no command leaves (§6.1)
    ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
    for (std::size_t pixel = 0; pixel < 16u; pixel++) {
        EXPECT_EQ(pixels[pixel * 4u + 0u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 1u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 2u], 0u);
        EXPECT_EQ(pixels[pixel * 4u + 3u], 0u);
    }
}
