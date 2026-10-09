#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text-skia/skia_text_engine.h"

using namespace albor;

namespace
{
    // the font file the vendored engine shapes from, and the family the renderer draws with (§9.1, AD-28)
    const char* const FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf";

    // the family the platform resolves for drawing, which must name that same face (§15, AD-28)
    const char* const FONT_FAMILY = "Arial";

    // the largest glyph position difference two shapers may disagree by, in logical pixels (§15, AD-25)
    constexpr float POSITION_TOLERANCE = 0.75f;

    // the largest advance difference the two shapers are allowed to disagree by (§15, AD-25)
    constexpr float WIDTH_TOLERANCE = 1.0f;

    // the largest difference the engines may report between their measured widths (§15, AD-25)
    bool has_font() {
        // the conformance suite needs a real font, and a host without one reports the absence (§9.1)
        std::ifstream probe(FONT_PATH, std::ios::binary);
        // the probe answers whether the font is present, which a host without one cannot shape against (§9.1)
        return probe.good();
    }

    // build a registered font over a font file, which is what generated code will emit for an embedded one (§11.3, AD-34)
    resources::EmbeddedFont make_embedded_font(std::string family, const char* path) {
        // the bytes are read whole, because a face is built from a complete font (§9.1)
        std::ifstream file(path, std::ios::binary);
        // a file that is not there carries no font, so the caller sees an empty registration (§9.1)
        if (!file)
            return resources::EmbeddedFont{.family = std::move(family)};
        // the bytes travel with the registration, which is what a generated font does (§11.3, AD-34)
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // the hash is computed by the registry, so a caller cannot register a wrong identity (§15)
        return resources::EmbeddedFont{.family = std::move(family), .content_hash = 0, .bytes = std::move(bytes)};
    }

    // the largest distance between two glyph positions, which the tolerance is written against (§15)
    float position_distance(Point left, Point right) {
        // the axes differ independently, so the larger one is the distance (§15)
        return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
    }
} // namespace

TEST(DualEngineConformanceChecks, TheVendoredEngineLoadsAFont) {
    // arrange, act
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    // assert the vendored engine is usable, because a suite that skips silently proves nothing (§15, §9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
}

TEST(DualEngineConformanceChecks, BothEnginesAgreeOnGlyphCount) {
    // arrange both engines shaping the same text at the same size (§15, AD-25)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    SkiaTextEngine oracle;
    // act the same string through both shapers, which is the dual engine comparison of AD-25 (§15)
    const TextLayout reference = oracle.measure("Albor framework", 0.0f, 20.0f);
    const TextLayout measured = vendored->measure("Albor framework", 0.0f, 20.0f);
    // assert both shaped something, because an empty run would agree with anything (§15)
    ASSERT_NE(reference.run, nullptr);
    ASSERT_NE(measured.run, nullptr);
    // assert the glyph counts match, which is the first thing a different shaper gets wrong (§15, AD-25)
    EXPECT_EQ(measured.run->glyph_count(), reference.run->glyph_count());
}

TEST(DualEngineConformanceChecks, BothEnginesAgreeOnGlyphPositions) {
    // arrange the same text through both shapers (§15, AD-25)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    SkiaTextEngine oracle;
    const TextLayout reference = oracle.measure("Albor framework", 0.0f, 20.0f);
    const TextLayout measured = vendored->measure("Albor framework", 0.0f, 20.0f);
    // assert both produced one line, because a differing line count is a failure before any position (§15)
    ASSERT_EQ(reference.run->lines().size(), 1u);
    ASSERT_EQ(measured.run->lines().size(), 1u);
    const ShapedRun::Line& reference_line = reference.run->lines().front();
    const ShapedRun::Line& measured_line = measured.run->lines().front();
    ASSERT_EQ(measured_line.glyphs.size(), reference_line.glyphs.size());
    // act every glyph position is compared, because a frame is wrong anywhere (§15)
    for (std::size_t index = 0; index < measured_line.glyphs.size(); index++) {
        // act
        const float distance = position_distance(measured_line.glyphs[index].position, reference_line.glyphs[index].position);
        // assert each glyph lands within the tolerance, which is what makes one shaper gate the other (§15)
        EXPECT_LE(distance, POSITION_TOLERANCE) << "glyph " << index << " differed by " << distance;
    }
}

TEST(DualEngineConformanceChecks, BothEnginesAgreeOnMeasuredWidth) {
    // arrange
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    SkiaTextEngine oracle;
    // act the same string is measured by both (§15, AD-25)
    const TextLayout reference = oracle.measure("Albor framework", 0.0f, 20.0f);
    const TextLayout measured = vendored->measure("Albor framework", 0.0f, 20.0f);
    // assert the widths agree, because a width drives every layout decision downstream (§8.2, AD-25)
    EXPECT_NEAR(measured.size.width, reference.size.width, WIDTH_TOLERANCE);
}

TEST(DualEngineConformanceChecks, BothEnginesAgreeOnAccentsAndPunctuation) {
    // arrange text that exercises shaping rather than simple advances (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    SkiaTextEngine oracle;
    const std::string text = "café naïve (résumé) 1,234.56";
    // act
    const TextLayout reference = oracle.measure(text, 0.0f, 18.0f);
    const TextLayout measured = vendored->measure(text, 0.0f, 18.0f);
    // assert both shaped the run, which is the case a naive shaper gets wrong (§9.1)
    ASSERT_NE(reference.run, nullptr);
    ASSERT_NE(measured.run, nullptr);
    // assert the glyph counts agree, because a combining sequence is where shapers differ most (§15)
    EXPECT_EQ(measured.run->glyph_count(), reference.run->glyph_count());
}

TEST(DualEngineConformanceChecks, TheVendoredEngineIsReproducible) {
    // arrange one engine asked twice, which is what a conformance suite leans on (§15, §9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    // act
    const TextLayout first = vendored->measure("repeatable", 0.0f, 16.0f);
    const TextLayout second = vendored->measure("repeatable", 0.0f, 16.0f);
    // assert the two runs are equal, because a shaper that varied could not gate anything (§15, AD-25)
    ASSERT_NE(first.run, nullptr);
    ASSERT_NE(second.run, nullptr);
    EXPECT_EQ(*first.run, *second.run);
}

TEST(DualEngineConformanceChecks, AnAbsentFontIsRefusedRatherThanHalfBuilt) {
    // arrange, act a path that names no font, which is what a refused engine must answer (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create("/no/such/font.ttf", FONT_FAMILY);
    // assert an engine with no face is refused, because a half built one would shape nothing silently (§9.1)
    EXPECT_EQ(engine, nullptr);
}

TEST(DualEngineConformanceChecks, AnEmptyStringMeasuresNothing) {
    // arrange
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    // act an empty string carries no glyph, which is what an empty layout means (§6.2)
    const TextLayout measured = vendored->measure("", 0.0f, 16.0f);
    // assert
    EXPECT_EQ(measured.run, nullptr);
}

TEST(DualEngineConformanceChecks, TheVendoredRunIsDrawable) {
    // arrange
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(vendored, nullptr);
    const TextLayout measured = vendored->measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(measured.run, nullptr);
    // act assert the run carries a typeface, which is what phase 3 needed before the oracle could go (§9.1, AD-28)
    EXPECT_TRUE(vendored->drawable());
    // the handle is valid for the same reason, so the renderer has something to draw the glyphs with (§9.1)
    EXPECT_TRUE(measured.run->lines().front().font.valid());
}

TEST(DualEngineConformanceChecks, AnUnknownFamilyLeavesTheEngineUnusable) {
    // arrange, act a family the platform does not have, which is the shape of a missing font (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, "NoSuchFamilyAlbor");
    // assert
    EXPECT_EQ(engine, nullptr);
}

TEST(DualEngineConformanceChecks, TheOracleRunIsDrawable) {
    // arrange the oracle issues a typeface and so draws (§9.1, AD-28)
    SkiaTextEngine oracle;
    const TextLayout reference = oracle.measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(reference.run, nullptr);
    // act assert the oracle is the drawable one, which is why it is kept as the oracle (§9.1)
    EXPECT_TRUE(reference.run->lines().front().font.valid());
}

TEST(EmbeddedFontChecks, ARegisteredFontIsFoundByFamilyAndByHash) {
    // arrange the registry starts empty, because a registration from another test would answer this (§2.2)
    resources::FontRegistry::clear();
    EXPECT_EQ(resources::FontRegistry::count(), 0u);
    // act a font is registered the way generated code registers one (§11.3, AD-34)
    resources::FontRegistry::register_font(make_embedded_font("AlborTest", FONT_PATH));
    // assert both lookups answer, since a family is asked for and a hash is what a layout keys on (§15)
    ASSERT_NE(resources::FontRegistry::find_family("AlborTest"), nullptr);
    const resources::EmbeddedFont* found = resources::FontRegistry::find_family("AlborTest");
    EXPECT_NE(resources::FontRegistry::find_hash(found->content_hash), nullptr);
    EXPECT_EQ(resources::FontRegistry::count(), 1u);
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, TheHashIsOverTheBytesAndNotTheName) {
    // arrange the same bytes registered under two names (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font("AlborOne", FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborTwo", FONT_PATH));
    const resources::EmbeddedFont* one = resources::FontRegistry::find_family("AlborOne");
    const resources::EmbeddedFont* two = resources::FontRegistry::find_family("AlborTwo");
    ASSERT_NE(one, nullptr);
    ASSERT_NE(two, nullptr);
    // act assert the names differ and the identity does not, because the identity is the bytes (§15, AD-34)
    EXPECT_EQ(one->content_hash, two->content_hash);
    EXPECT_NE(one->content_hash, 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, ARefusedFontIsNotRegistered) {
    // arrange the registry starts empty (§2.2)
    resources::FontRegistry::clear();
    // act a font with no bytes cannot shape, and one with no name cannot be asked for (§9.1)
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = "Empty", .bytes = {}});
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = "", .bytes = {1, 2, 3}});
    // assert neither joined, because a half registered font is worse than none (§9.1)
    EXPECT_EQ(resources::FontRegistry::count(), 0u);
    EXPECT_EQ(resources::FontRegistry::find_family("Empty"), nullptr);
}

TEST(EmbeddedFontChecks, AnUnknownFamilyIsRefusedRatherThanFallingBack) {
    // arrange the registry carries one font, so a fallback would be visible (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    // act an engine over a family the build does not carry (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded("AlborAbsent");
    // assert the engine is refused rather than shaping with the carried font, which would break identity (§15, AD-34)
    EXPECT_EQ(engine, nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, AnEmbeddedFontShapesExactlyAsTheSameFileDoes) {
    // arrange one engine reads the font from the build and one from the file, which is the claim (§15, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> embedded = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> from_file = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(embedded, nullptr);
    ASSERT_NE(from_file, nullptr);
    // act the same strings are shaped by both, which is the claim AD-34 makes about the source of the font (§15)
    const std::string text = "Albor framework";
    const TextLayout embedded_layout = embedded->measure(text, 0.0f, 20.0f);
    const TextLayout file_layout = from_file->measure(text, 0.0f, 20.0f);
    // assert the two fonts identify as the same bytes, which is the whole claim (§15, AD-34)
    EXPECT_EQ(embedded->font_hash(), from_file->font_hash());
    ASSERT_NE(embedded_layout.run, nullptr);
    ASSERT_NE(file_layout.run, nullptr);
    // assert the layouts are identical, not merely close: this is layout identity and it has no tolerance (§15)
    EXPECT_EQ(*embedded_layout.run, *file_layout.run);
    EXPECT_FLOAT_EQ(embedded_layout.size.width, file_layout.size.width);
    EXPECT_FLOAT_EQ(embedded_layout.ascent, file_layout.ascent);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, AnEmbeddedEngineNeedsNoFileOnDisk) {
    // arrange the registry carries the bytes, so the engine has everything it needs (§15, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    // act the engine is built from the registry alone, with no path anywhere in the call (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> embedded = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    // assert it shapes, which is what makes the build self contained (§9.1)
    ASSERT_NE(embedded, nullptr);
    ASSERT_TRUE(embedded->ready());
    EXPECT_GT(embedded->font_hash(), 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AVisibleGlyphHasAnOutline) {
    // arrange an engine over the carried font, and the glyph index of a letter inside the string (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_GT(layout.run->glyph_count(), 0u);
    // act the first glyph is the letter, because a one letter string shapes one glyph (§9.1)
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 20.0f);
    // assert the outline is a real shape, because an empty outline would draw nothing (§6.2, §15)
    ASSERT_FALSE(outline.empty());
    EXPECT_GT(outline.size(), 3u);
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, ASpaceHasNoOutline) {
    // arrange a space shapes to a glyph that carries no outline (§6.2)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure(" ", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the space glyph is drawn as nothing, so it has no points (§6.2)
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 20.0f);
    // assert an empty outline is returned rather than a degenerate shape (§6.2)
    EXPECT_TRUE(outline.empty());
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AnOutlineIsReproducible) {
    // arrange one glyph asked for twice, which is the property AD-34 depends on (§15, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    // act the same glyph is walked twice from the same bytes (§15, AD-34)
    const std::vector<Point> first = engine->glyph_outline(glyph, 20.0f);
    const std::vector<Point> second = engine->glyph_outline(glyph, 20.0f);
    // assert the two outlines are identical, because the shape comes from the font and not from a rasterizer (§15)
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); index++) {
        // act assert each point matches exactly, since an outline carries no tolerance (§15, AD-34)
        EXPECT_FLOAT_EQ(first[index].x, second[index].x);
        // the vertical coordinate matches for the same reason (§15, AD-34)
        EXPECT_FLOAT_EQ(first[index].y, second[index].y);
    }
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AnOutlineScalesWithTheFontSize) {
    // arrange one glyph at two sizes, which is what the scale in the sink is for (§9.1, AD-27)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    // act
    const std::vector<Point> small = engine->glyph_outline(glyph, 20.0f);
    const std::vector<Point> large = engine->glyph_outline(glyph, 40.0f);
    // assert doubling the size doubles the shape, because the outline is in logical pixels (§9.1, AD-27)
    ASSERT_EQ(small.size(), large.size());
    float small_height = 0.0f;
    float large_height = 0.0f;
    for (std::size_t index = 0; index < small.size(); index++) {
        // the extents are accumulated because a glyph outline is not anchored at the origin (§9.1)
        small_height = std::max(small_height, std::abs(small[index].y));
        // the larger outline is measured the same way, so the ratio means something (§9.1)
        large_height = std::max(large_height, std::abs(large[index].y));
    }
    EXPECT_NEAR(large_height, small_height * 2.0f, 0.01f);
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AnUnknownGlyphHasNoOutline) {
    // arrange an engine whose face carries fewer glyphs than the index asks for (§6.2)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act an index past the end of the font cannot be loaded (§9.1)
    const std::vector<Point> outline = engine->glyph_outline(1000000u, 20.0f);
    // assert nothing is returned rather than a crash or a degenerate shape (§6.2)
    EXPECT_TRUE(outline.empty());
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, ACurveGlyphIsTessellatedRatherThanSampled) {
    // arrange a round letter, whose outline is a circle and whose every point sits at one radius (§6.4)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("o", 0.0f, 100.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the round letter is walked, which is where a conic or a cubic is reported (§6.4)
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 100.0f);
    ASSERT_FALSE(outline.empty());
    // assert a real tessellation happened, which a polyline through control points would not produce (§6.4, §15)
    EXPECT_GT(outline.size(), 32u);
    // act the widest gap between consecutive points is measured, which is what distinguishes a tessellation (§6.4)
    float widest_gap = 0.0f;
    float leftmost = outline.front().x;
    float rightmost = outline.front().x;
    for (std::size_t index = 1; index < outline.size(); index++) {
        // the gap is the distance between neighbours, which a curve walk keeps small and even (§6.4)
        const float gap = std::hypot(outline[index].x - outline[index - 1].x, outline[index].y - outline[index - 1].y);
        widest_gap = std::max(widest_gap, gap);
        // the extent is tracked, because the gap is measured against the glyph's own width (§6.4)
        leftmost = std::min(leftmost, outline[index].x);
        // the other edge is tracked beside it (§6.4)
        rightmost = std::max(rightmost, outline[index].x);
    }
    const float glyph_width = rightmost - leftmost;
    ASSERT_GT(glyph_width, 1.0f);
    // assert every gap is a small fraction of the glyph, which control points would not be (§15, AD-34)
    EXPECT_LT(widest_gap, glyph_width * 0.2f);
    // exit
    resources::FontRegistry::clear();
}
