#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkSurface.h>

#include "render/frame_scene.h"
#include "render/render-skia/skia_render_context.h"
#include "render/render-skia/skia_renderer_backend.h"
#include "text/text-skia/skia_text_engine.h"
#include "tools/alloc-probe/alloc_probe.h"

using namespace albor;
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

TEST(SkiaRendererBackendLifecycle, ConstructingAndDestroyingABackendReturns) {
    // act the backend is constructed and destroyed in a scope, which is what an application does (§6.3)
    {
        SkiaRendererBackend backend;
        // assert it came up, because a backend that did not is a different failure than the one guarded here (§6.3)
        EXPECT_TRUE(backend.ready());
    }
    // assert reaching this line is the claim: the driver traps in the queue dealloc unless the queue is held (§6.3)
    SUCCEED();
}

TEST(SkiaRendererBackendLifecycle, DestroyingABackendAfterItRecordedNothingStillReturns) {
    // arrange a second backend in the same process, because the first test already exercised one queue (§6.3)
    std::unique_ptr<SkiaRendererBackend> backend = std::make_unique<SkiaRendererBackend>();
    // assert it came up, so the destruction below is not skipped by a null (§6.3)
    ASSERT_TRUE(backend->ready());
    // act
    backend.reset();
    // assert returning at all is the claim, because the teardown order is what the driver rejects (§6.3)
    SUCCEED();
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
