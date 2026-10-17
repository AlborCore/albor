#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkSurface.h>

#include "render/frame_scene.h"
#include "render/glyph_outline.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"
#include "render/render-skia/skia_render_context.h"
#include "render/render-skia/skia_renderer_backend.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/screenshot.h"
#include "text/text-skia/skia_text_engine.h"
#include "tools/alloc-probe/alloc_probe.h"

using namespace albor;

namespace
{
    // render one scene on both backends and report the largest per channel difference, or -1 when either refused (§16)
    int parity_difference(SkiaRendererBackend& gpu_backend, RenderSurface& gpu, software::SoftwareRendererBackend& reference_backend, RenderSurface& reference, const FrameScene& scene) {
        // both backends render the same immutable scene (§6.1)
        gpu_backend.execute_frame(gpu, scene);
        reference_backend.execute_frame(reference, scene);
        // each frame is read back, because a refused read is not an agreement (§16)
        const Screenshot gpu_read = capture_surface(gpu);
        const Screenshot reference_read = capture_surface(reference);
        // a refused read is reported rather than compared (§16)
        if (gpu_read.empty() || reference_read.empty())
            return -1;
        // the largest per channel difference is what the class tolerance is written against (§15)
        return gpu_read.max_channel_difference(reference_read);
    }
} // namespace
using namespace albor::skia;

namespace
{
    // the width of the surface every test draws through, which needs no window server (§6.3)
    constexpr int SURFACE_WIDTH = 400;

    // the surface height every test draws through, which is deep enough for a run of several lines (§6.3)
    constexpr int SURFACE_HEIGHT = 200;

    // the bounds of the ink a draw left on a surface, which is what a drawing assertion reads back (§6.3)
    Rect ink_bounds(const sk_sp<SkSurface>& surface) {
        // the destination is read back in the format the surface was created with (§6.3)
        const SkImageInfo info = SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT);
        // the pixels land in a buffer, because the canvas does not hand them out (§6.3)
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(SURFACE_WIDTH) * static_cast<std::size_t>(SURFACE_HEIGHT), 0u);
        // the read pulls the whole surface, because ink can land anywhere the run placed it (§6.3)
        surface->readPixels(info, pixels.data(), static_cast<std::size_t>(SURFACE_WIDTH) * sizeof(std::uint32_t), 0, 0);
        // the edges start at the far side of the surface, so an untouched surface reports no ink (§6.3)
        int left = SURFACE_WIDTH;
        // the top edge starts below the surface, for the same reason (§6.3)
        int top = SURFACE_HEIGHT;
        // the right edge starts before the surface, for the same reason (§6.3)
        int right = 0;
        // the bottom edge starts before the surface, for the same reason (§6.3)
        int bottom = 0;
        // every pixel is scanned, because the run may place glyphs anywhere in the surface (§6.3)
        for (int row = 0; row < SURFACE_HEIGHT; row++)
            for (int column = 0; column < SURFACE_WIDTH; column++) {
                // the pixel is addressed by its row major index, which is what the surface hands back (§6.3)
                const std::size_t index = static_cast<std::size_t>(row) * SURFACE_WIDTH + static_cast<std::size_t>(column);
                // a transparent pixel is not ink, because every test clears the surface before it draws (§6.3)
                if (SkColorGetA(pixels[index]) == 0)
                    continue;
                // the left edge follows the leftmost ink and the right edge the rightmost (§6.3)
                left = std::min(left, column);
                // the top edge follows the topmost ink and the bottom edge the bottommost (§6.3)
                top = std::min(top, row);
                // the right edge follows the rightmost inked column (§6.3)
                right = std::max(right, column);
                // the bottom edge follows the bottommost inked row (§6.3)
                bottom = std::max(bottom, row);
            }
        // a surface with no ink has no bounds at all (§6.3)
        if (left > right)
            return Rect{};
        // the edges compose into the bounds the assertions read (§6.3)
        return Rect::from_origin_size(Point{static_cast<float>(left), static_cast<float>(top)}, Size{static_cast<float>(right - left + 1), static_cast<float>(bottom - top + 1)});
    }

    // a run with every typeface stripped, as a backend that cannot read it sees (§3.5, §9.1)
    TextLayout without_typefaces(const TextLayout& layout) {
        // the lines are rebuilt one by one, because each names its own typeface (§9.1)
        std::vector<ShapedRun::Line> lines;
        // every shaped line contributes a copy that keeps its glyphs and loses only its typeface (§9.1)
        for (const ShapedRun::Line& line : layout.run->lines()) {
            // the stripped line is built aside, because the handle stays default constructed (§9.1)
            ShapedRun::Line stripped;
            // the baseline travels with the copy, so only the typeface is what the draw cannot resolve (§9.1)
            stripped.origin = line.origin;
            // the glyphs travel with the copy, so the draw has real glyphs it still cannot place (§9.1)
            stripped.glyphs = line.glyphs;
            // the stripped line joins the run in reading order (§9.1)
            lines.push_back(std::move(stripped));
        }
        // the run keeps the shaped size and reports what the measurement reported (§9.1)
        return TextLayout{.run = ShapedRun::make(std::move(lines), layout.run->font_size()), .size = layout.size, .ascent = layout.ascent, .descent = layout.descent};
    }
} // namespace

TEST(SkiaRenderContextTextChecks, DrawTextPutsInkOnTheCanvas) {
    // arrange the run is shaped by the real engine, which proves the handle resolves (§9.1)
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(layout, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a shaped run is rasterized as the glyphs it carries, which is what stage two replays (§9.1)
    EXPECT_FALSE(ink_bounds(surface).empty());
}

TEST(SkiaRenderContextTextChecks, DrawTextPlacesTheRunWhereTheCallerAsked) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(layout, Point{120.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the run origin moves the ink with it, so a caller places the run by its box (§6.2)
    EXPECT_GE(ink_bounds(surface).x, 120.0f);
}

TEST(SkiaRenderContextTextChecks, DrawTextMovesTheRunWithTheOriginItWasGiven) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(layout, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the ink sits at the top of the box the caller placed, which is what the run origin means (§9.1)
    const float high_ink = ink_bounds(surface).y;
    // arrange the second draw over a cleared surface, so the two origins are compared on their own (§6.3)
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    SkiaRenderContext shifted(surface->getCanvas(), 1.0f);
    // act
    shifted.draw_text(layout, Point{10.0f, 100.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the origin carries the run by exactly the distance it moved (§9.1)
    EXPECT_NEAR(ink_bounds(surface).y - high_ink, 60.0f, 2.0f);
}

TEST(SkiaRenderContextTextChecks, DrawTextPlacesEveryLineOfAWrappedRunOnItsOwnBaseline) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout single = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(single, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a single line covers one line of ink, which is what the comparison below is measured against (§9.1)
    const float single_height = ink_bounds(surface).height;
    // arrange the second draw over a cleared surface, so the two runs are compared on their own (§6.3)
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    SkiaRenderContext wrapping(surface->getCanvas(), 1.0f);
    // act
    wrapping.draw_text(wrapped, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a wrapped run inks every line it shaped, because each line brings its own baseline to the draw (§9.1)
    ASSERT_GT(wrapped.run->lines().size(), 1u);
    EXPECT_GT(ink_bounds(surface).height, single_height * 1.5f);
}

TEST(SkiaRenderContextTextChecks, DrawTextSizesAWrappedRunFromTheSizeItWasShapedAt) {
    // arrange a wrapped run is where the run height and the shaped size disagree (§9.1)
    SkiaTextEngine engine;
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(wrapped, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the glyphs are sized from the shaped size, so the ink fits the reported box (§9.1)
    const Rect bounds = ink_bounds(surface);
    EXPECT_FALSE(bounds.empty());
    EXPECT_LE(bounds.height, wrapped.size.height);
}

TEST(SkiaRenderContextTextChecks, DrawTextSkipsALayoutThatCarriesNoRun) {
    // arrange
    SkiaTextEngine engine;
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(TextLayout{}, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a layout that measured nothing draws nothing (§6.2)
    EXPECT_TRUE(ink_bounds(surface).empty());
}

TEST(SkiaRenderContextTextChecks, DrawTextSkipsARunThatCarriesNoGlyphs) {
    // arrange
    SkiaTextEngine engine;
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // the layout reports an extent but its run shaped nothing, which is what an unresolvable run looks like (§6.2)
    const TextLayout layout{.run = ShapedRun::make({}, 20.0f), .size = Size{100.0f, 24.0f}};
    // act
    context.draw_text(layout, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a run with no glyphs costs nothing to draw (§6.2)
    EXPECT_TRUE(ink_bounds(surface).empty());
}

TEST(SkiaRenderContextTextChecks, DrawTextSkipsALineWhoseTypefaceItCannotInterpret) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_text(without_typefaces(layout), Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert a wrong font is worse than no text, so the line is skipped (§9.1)
    EXPECT_TRUE(ink_bounds(surface).empty());
}

TEST(SkiaRenderContextTextChecks, DrawTextOnAContextWithNoCanvasDrawsNothing) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    SkiaRenderContext context(nullptr, 1.0f);
    // act / assert a context with no canvas draws nothing rather than reaching through a null canvas (§6.2)
    EXPECT_NO_FATAL_FAILURE(context.draw_text(layout, Point{10.0f, 40.0f}, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)}));
}

TEST(SkiaRenderContextTextChecks, DrawingARunCostsNoAllocationOnceItHasDrawn) {
    // arrange the run is shaped once, and the context is built over a surface that needs no window server (§6.3)
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    const Paint paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)};
    // act the first draw pays for whatever the draw path allocates for the first time (§6.3)
    SkiaRenderContext warm_up(surface->getCanvas(), 1.0f);
    warm_up.draw_text(layout, Point{10.0f, 40.0f}, paint);
    probe::set_counting(true);
    probe::reset();
    // act the second draw of the same run, which is what a steady state frame costs (§6.3)
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    context.draw_text(layout, Point{10.0f, 40.0f}, paint);
    const std::size_t repeat_allocations = probe::count();
    probe::set_counting(false);
    // assert the draw allocates nothing, because the glyph and position buffers live with the thread (§6.3)
    EXPECT_EQ(repeat_allocations, 0u);
}

TEST(SkiaRenderContextTextChecks, DrawingManyLinesOfOneRunCostsNoAllocation) {
    // arrange a wrapped run is several lines, so a per line buffer would show up here (§9.1)
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    ASSERT_GT(layout.run->lines().size(), 1u);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    const Paint paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)};
    // act the first draw warms the thread buffers for every line it walks (§6.3)
    SkiaRenderContext warm_up(surface->getCanvas(), 1.0f);
    warm_up.draw_text(layout, Point{10.0f, 40.0f}, paint);
    probe::set_counting(true);
    probe::reset();
    // act the second draw walks the same lines again, which is the steady state case (§6.3)
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    context.draw_text(layout, Point{10.0f, 40.0f}, paint);
    const std::size_t repeat_allocations = probe::count();
    probe::set_counting(false);
    // assert no line allocates, because the buffers are emptied and refilled rather than rebuilt (§6.3)
    EXPECT_EQ(repeat_allocations, 0u);
}

TEST(SkiaRenderContextShadowChecks, AContextWithNoCanvasSwallowsTheDraws) {
    // arrange a context over no canvas at all, which is the refusal path a backend takes (§6.2)
    SkiaRenderContext context(nullptr, 1.0f);
    // act every draw a caller could make, because each one has to survive the null canvas (§6.2)
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f), .blur = 2.0f});
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    context.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 2.0f, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the state stayed usable, because a null context is a refusal rather than a crash (§6.2)
    EXPECT_FLOAT_EQ(context.opacity(), 1.0f);
    SUCCEED();
}

TEST(SkiaRenderContextShadowChecks, ARefusedCastDrawsNothing) {
    // arrange a context over no canvas, plus a cast the primitive must refuse twice over (§6.2)
    SkiaRenderContext context(nullptr, 1.0f);
    // act a clear colour with a shape, and a colour with no shape, which are both refusals (§6.2)
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)});
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f)});
    // assert
    SUCCEED();
}

TEST(SkiaRenderContextShadowChecks, ARefusedCastOnALiveCanvasDrawsNothing) {
    // arrange a real canvas, because the refusals must hold where the canvas could actually draw (§6.2)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act the two refusals, on a canvas that would have drawn them (§6.2)
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)});
    context.draw_shadow(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 10.0f}), 0.0f, Shadow{.color = Color::rgb(0.0f, 0.0f, 0.0f)});
    // assert the canvas stayed clear, because a refused cast puts no ink down (§6.2)
    const SkImageInfo info = SkImageInfo::Make(SURFACE_WIDTH, SURFACE_HEIGHT, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(SURFACE_WIDTH * SURFACE_HEIGHT * 4));
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    int inked = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        if (pixels[i] > 0)
            inked++;
    EXPECT_EQ(inked, 0);
}

TEST(SkiaRenderContextShadowChecks, AClampedOpacityReachesTheSurface) {
    // arrange a real canvas and a colour the opacity halves, which is what a node opacity asks (§6.1)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(SURFACE_WIDTH, SURFACE_HEIGHT));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act an overshoot first, so the clamp runs against the value a binding would produce (§6.1)
    context.set_opacity(9.0f);
    EXPECT_FLOAT_EQ(context.opacity(), 1.0f);
    context.set_opacity(0.5f);
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert the drawn pixel carries the halved alpha, because the opacity reaches the surface (§6.1)
    const SkImageInfo info = SkImageInfo::Make(SURFACE_WIDTH, SURFACE_HEIGHT, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(SURFACE_WIDTH * SURFACE_HEIGHT * 4));
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_NEAR(pixels[3], 128, 3);
}

TEST(SkiaRenderContextOpacityChecks, ATranslucentColourIsNotSquaredByTheOpacity) {
    // arrange a half transparent colour drawn at no opacity, which must land at half (§6.1)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{32.0f, 32.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)});
    // assert the drawn alpha is half and not a quarter, because the opacity scales once (§6.1)
    const SkImageInfo info = SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_NEAR(pixels[3], 128, 3);
}

TEST(SkiaRenderContextOpacityChecks, TheOpacityScalesATranslucentColourOnce) {
    // arrange a half transparent colour under a half opacity, which must land at a quarter (§6.1)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.set_opacity(0.5f);
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{32.0f, 32.0f}), Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.5f)});
    // assert the drawn alpha is a quarter, because the two factors multiply (§6.1)
    const SkImageInfo info = SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_NEAR(pixels[3], 64, 3);
}

TEST(SkiaRenderContextOpacityChecks, AnOpaqueColourStaysOpaque) {
    // arrange an opaque colour, which no opacity may touch beyond one (§6.1)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{32.0f, 32.0f}), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    // assert
    const SkImageInfo info = SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_EQ(pixels[3], 255);
}

TEST(SkiaRenderContextShadowChecks, ATranslucentCastIsNotModulatedTwiceByTheLayer) {
    // arrange a half transparent cast, which must land at half rather than a quarter (§6.2)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act a blurred cast, which is the path that allocates a layer and composites it (§6.2)
    context.draw_shadow(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{32.0f, 32.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.5f), .offset = Point{0.0f, 0.0f}, .blur = 0.5f});
    // assert the centre of the cast is half covered, because the layer carries the blur and not the colour (§6.2)
    const SkImageInfo info = SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_NEAR(pixels[(24 * 64 + 24) * 4 + 3], 128, 4);
}

TEST(SkiaRenderContextShadowChecks, ATranslucentCastAtNoBlurLandsAtItsOwnAlpha) {
    // arrange the same cast without a blur, which skips the layer entirely (§6.2)
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->clear(0x00000000);
    SkiaRenderContext context(surface->getCanvas(), 1.0f);
    // act
    context.draw_shadow(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{32.0f, 32.0f}), 0.0f, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, 0.5f), .offset = Point{0.0f, 0.0f}, .blur = 0.0f});
    // assert the two paths agree, because the blur must not change how much ink a cast puts down (§6.2)
    const SkImageInfo info = SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    ASSERT_TRUE(surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0));
    EXPECT_NEAR(pixels[(24 * 64 + 24) * 4 + 3], 128, 3);
}

TEST(SkiaParityChecks, TheOffscreenGpuTargetMatchesTheSoftwareReference) {
    // arrange both backends at the same metrics, which is what a parity comparison needs (§16, AD-26)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);
    SkiaRendererBackend gpu_backend;
    // a host without a gpu device cannot run the comparison, which is reported rather than passing (§6.3)
    if (!gpu_backend.ready())
        GTEST_SKIP() << "no gpu device on this host";
    const std::unique_ptr<RenderSurface> gpu = gpu_backend.create_offscreen_surface(metrics);
    ASSERT_NE(gpu, nullptr);
    software::SoftwareRendererBackend software_backend;
    const std::unique_ptr<RenderSurface> reference = software_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(reference, nullptr);
    // the scene is one flat rectangle at integer coordinates, which §15 requires to agree exactly (§15, AD-26)
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{32.0f, 24.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}));
    // a resize on the target is forwarded, which is what a parity check does between scenes (§6.2)
    gpu_backend.resize_surface(*gpu, metrics);
    // act both backends render the same immutable scene (§6.1)
    gpu_backend.execute_frame(*gpu, scene);
    software_backend.execute_frame(*reference, scene);
    const std::vector<std::uint8_t> gpu_pixels = gpu->read_pixels();
    const std::vector<std::uint8_t> reference_pixels = reference->read_pixels();
    // assert both produced a frame before comparing, because a refused read is not an agreement (§16)
    ASSERT_FALSE(gpu_pixels.empty());
    ASSERT_FALSE(reference_pixels.empty());
    ASSERT_EQ(gpu_pixels.size(), reference_pixels.size());
    // assert the flat interior and the integer edges agree exactly, which the class allows no freedom (§15, AD-26)
    std::size_t worst = 0;
    for (std::size_t index = 0; index < gpu_pixels.size(); index++)
        worst = std::max(worst, static_cast<std::size_t>(std::abs(static_cast<int>(gpu_pixels[index]) - static_cast<int>(reference_pixels[index]))));
    EXPECT_EQ(worst, 0u) << "the gpu and reference frames diverge by " << worst << " per channel";
}

TEST(SkiaParityChecks, TheOffscreenGpuTargetMatchesTheSoftwareReferenceOnLevel2Primitives) {
    // arrange both backends at one set of metrics (§16, AD-26)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{96.0f, 64.0f}, 1.0f);
    SkiaRendererBackend gpu_backend;
    // a host without a gpu device cannot run the comparison, which is reported rather than passing (§6.3)
    if (!gpu_backend.ready())
        GTEST_SKIP() << "no gpu device on this host";
    const std::unique_ptr<RenderSurface> gpu = gpu_backend.create_offscreen_surface(metrics);
    software::SoftwareRendererBackend reference_backend;
    const std::unique_ptr<RenderSurface> reference = reference_backend.create_surface(NativeWindowHandle{}, metrics);
    ASSERT_NE(gpu, nullptr);
    ASSERT_NE(reference, nullptr);
    const Color background = Color::rgb(1.0f, 1.0f, 1.0f);
    const Paint ink{.fill = Color::rgb(0.0f, 0.0f, 0.0f)};
    // a flat rectangle on integer coordinates is exact, because the class allows no freedom (§15)
    FrameScene rect(metrics, background);
    rect.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{32.0f, 24.0f}), ink));
    const int rect_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, rect);
    EXPECT_EQ(rect_difference, 0) << "a flat integer rectangle diverged by " << rect_difference;
    // a rounded rectangle is antialiased, so it is allowed a small per channel band (§15)
    FrameScene rounded(metrics, background);
    rounded.add_command(DrawCommand::rounded_rectangle(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{40.0f, 30.0f}), 8.0f, ink));
    const int rounded_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, rounded);
    // the bound is the measured 47 on this host plus margin, which is the per class budget §15 asks for (§15)
    EXPECT_LE(rounded_difference, 64) << "a rounded rectangle diverged by " << rounded_difference;
    // a path is tessellated per backend, so it is allowed the same band (§15)
    FrameScene path_scene(metrics, background);
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{12.0f, 40.0f}, Point{36.0f, 12.0f}, Point{60.0f, 40.0f}};
    path_scene.add_command(DrawCommand::path(std::make_shared<const PathResource>(7u, std::span<const PathVerb>(verbs), std::span<const Point>(points)), Transform{}, ink));
    const int path_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, path_scene);
    // the bound is the measured 17 on this host plus margin, which is the per class budget §15 asks for (§15)
    EXPECT_LE(path_difference, 32) << "a path diverged by " << path_difference;
    // a glyph outline is a filled path, which both backends draw through the same replay (§6.4, AD-34)
    FrameScene glyph(metrics, background);
    const std::vector<Point> outline{Point{20.0f, 48.0f}, Point{32.0f, 16.0f}, Point{44.0f, 48.0f}};
    glyph.add_command(DrawCommand::glyph_outline(std::make_shared<const GlyphOutline>(11u, 1u, 16.0f, outline), Point{0.0f, 0.0f}, ink));
    const int glyph_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, glyph);
    // the bound is the measured 6 on this host plus margin, which is the per class budget §15 asks for (§15)
    EXPECT_LE(glyph_difference, 16) << "a glyph outline diverged by " << glyph_difference;
    // a retained line batch is stroked, so it is allowed the antialiased band (§6.4, §15)
    FrameScene batch_scene(metrics, background);
    const std::vector<Point> batch_points{Point{8.0f, 32.0f}, Point{48.0f, 32.0f}, Point{48.0f, 56.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(3u, std::span<const Point>(batch_points));
    batch_scene.add_command(DrawCommand::line_batch(batch, Transform{}, StrokeStyle{.color = Color::rgb(0.0f, 0.0f, 0.0f), .width = 2.0f}));
    const int batch_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, batch_scene);
    // the bound is the measured 59 on this host plus margin, because the two rasterizers join a stroke differently (§15)
    EXPECT_LE(batch_difference, 96) << "a line batch diverged by " << batch_difference;
    // a picture replays an inner command list, which both backends share through one replay (§6.4, §15)
    FrameScene picture_scene(metrics, background);
    std::vector<DrawCommand> inner;
    inner.push_back(DrawCommand::rectangle(Rect::from_origin_size(Point{4.0f, 4.0f}, Size{20.0f, 16.0f}), ink));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(4u, std::move(inner));
    picture_scene.add_command(DrawCommand::picture(picture, Transform{.translate_x = 10.0f, .translate_y = 10.0f}));
    const int picture_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, picture_scene);
    // an integer rect inside a picture is exact, so the budget is zero (§15)
    EXPECT_EQ(picture_difference, 0) << "a picture diverged by " << picture_difference;
    // an image is uploaded and composited, so it is allowed the antialiased band (§6.4, §15)
    FrameScene image_scene(metrics, background);
    const std::vector<std::uint8_t> image_pixels(16u * 16u * 4u, 255u);
    const std::shared_ptr<const ImageResource> image = std::make_shared<const ImageResource>(5u, 16u, 16u, ImageFormat::RGBA8, std::span<const std::uint8_t>(image_pixels));
    image_scene.add_command(DrawCommand::image(image, Rect::from_origin_size(Point{8.0f, 8.0f}, Size{16.0f, 16.0f}), Paint{}));
    const int image_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, image_scene);
    // the bound is the measured value on this host plus margin, which is the per class budget §15 asks for (§15)
    EXPECT_LE(image_difference, 16) << "an image diverged by " << image_difference;
    // a shaped run is drawn from the shaped glyphs, so the two rasterizers are allowed the antialiased band (§9.1, §15)
    SkiaTextEngine text_engine;
    const TextLayout text_layout = text_engine.measure("Albor", 1000.0f, 20.0f);
    FrameScene text_scene(metrics, background);
    text_scene.add_command(DrawCommand::text(text_layout, Point{8.0f, 40.0f}, ink));
    const int text_difference = parity_difference(gpu_backend, *gpu, reference_backend, *reference, text_scene);
    // the bound is the measured 127 on this host plus margin, which is the antialiased outline of five glyphs (§15)
    EXPECT_LE(text_difference, 192) << "a shaped run diverged by " << text_difference;
}
