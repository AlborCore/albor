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
#include "text/text-skia/skia_text_engine.h"

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
                // a transparent pixel is not ink, because every test clears the surface before it draws (§6.3)
                if (SkColorGetA(pixels[static_cast<std::size_t>(row) * SURFACE_WIDTH + column]) == 0)
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
