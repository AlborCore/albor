#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/draw_command.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/screenshot.h"
#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/embedded_glyph_provider.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text-skia/skia_text_engine.h"
#include "ui/frame_pipeline.h"
#include "ui/scene_builder.h"
#include "window/window_metrics.h"

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

TEST(DualEngineConformanceChecks, ANonFontIsRefusedRatherThanHalfBuilt) {
    // arrange the registry carries bytes that name no face, which is the shape of a corrupt build (§9.1)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = "AlborGarbage", .content_hash = 0, .bytes = {1, 2, 3, 4, 5, 6, 7, 8}});
    // act an engine is built over bytes no face can read (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded("AlborGarbage");
    // assert the engine is refused, because a face with no glyph shapes nothing and must not be half built (§9.1)
    EXPECT_EQ(engine, nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ADefaultSpecMeasuresAsTheSizeOnlyOverload) {
    // arrange an engine that shapes real text, which the two overloads must measure identically (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same run is measured with a size only and with a default font selection (§9.1, AD-34)
    const TextLayout plain = engine->measure("Albor", 0.0f, 16.0f);
    const TextLayout with_spec = engine->measure("Albor", 0.0f, 16.0f, FontSpec{});
    // assert the default selection forwards exactly, so the overload changed no measurement (§9.1)
    ASSERT_NE(plain.run, nullptr);
    ASSERT_NE(with_spec.run, nullptr);
    EXPECT_EQ(*plain.run, *with_spec.run);
    EXPECT_EQ(plain.size, with_spec.size);
}

TEST(DualEngineConformanceChecks, ABackendPublishesWhetherItResolvesAFontSelection) {
    // arrange an engine over the carried font, which does not resolve a family beyond its own face yet (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act
    const bool resolves = engine->resolves_font_selection();
    // assert the limitation is published, so no caller passes a family and silently gets the default face (§15)
    EXPECT_FALSE(resolves);
}

TEST(DualEngineConformanceChecks, ANonDefaultSpecCurrentlyMeasuresAsTheDefaultFace) {
    // arrange an engine that has not learned to resolve a family, which the current contract must state plainly (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a run is measured once with no selection and once with a family and weight the backend cannot resolve (§9.1)
    const TextLayout plain = engine->measure("Albor", 0.0f, 16.0f);
    const TextLayout asked = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "Inter", .weight = FontWeight::Bold});
    // assert the current limitation is pinned, so a backend that resolves a family updates this test (§9.1)
    ASSERT_NE(plain.run, nullptr);
    ASSERT_NE(asked.run, nullptr);
    EXPECT_EQ(*plain.run, *asked.run);
}

TEST(EmbeddedFontChecks, TheOflFixtureIsPresentAndIdentifiedByItsBytes) {
    // arrange the test font the tree carries, which gives the suites a font no host has to supply (§15, AD-34, #92)
    const resources::EmbeddedFont fixture = make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH);
    // assert the fixture carries a real sfnt, which a text file renamed to ttf would not (§9.1)
    ASSERT_GE(fixture.bytes.size(), 4u);
    EXPECT_EQ(fixture.bytes[0], 0x00u);
    EXPECT_EQ(fixture.bytes[1], 0x01u);
    EXPECT_EQ(fixture.bytes[2], 0x00u);
    EXPECT_EQ(fixture.bytes[3], 0x00u);
    // act the same bytes are registered under two names, which is what identifies a font by content (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(fixture);
    resources::FontRegistry::register_font(make_embedded_font("AlborFixtureAlias", ALBOR_TEST_FONT_PATH));
    // assert the two names resolve and share one identity, because identity is the bytes (§15, AD-34)
    const resources::EmbeddedFont* named = resources::FontRegistry::find_family(ALBOR_TEST_FONT_FAMILY);
    const resources::EmbeddedFont* alias = resources::FontRegistry::find_family("AlborFixtureAlias");
    ASSERT_NE(named, nullptr);
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(named->content_hash, alias->content_hash);
    EXPECT_NE(named->content_hash, 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, AnEmbeddedFixtureShapesWithoutAPlatformFont) {
    // arrange the carried test font under its family, which the engine must shape without a platform font (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    // act an engine is built over the carried bytes alone, which must not consult a platform font (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    // assert the engine is ready and shapes the fixture, which is what removes the host dependency (#44, #92)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    const TextLayout layout = engine->measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(layout.run, nullptr);
    EXPECT_GT(layout.run->glyph_count(), 0u);
    // assert the run carries no typeface and still draws, because the renderer walks its outlines (§15, AD-34)
    EXPECT_FALSE(layout.run->lines().front().font.valid());
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 16.0f);
    EXPECT_FALSE(outline.empty());
    // exit
    resources::FontRegistry::clear();
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
    // assert the lines match field for field, since layout identity carries no tolerance (§15)
    ASSERT_EQ(embedded_layout.run->lines().size(), file_layout.run->lines().size());
    for (std::size_t index = 0; index < embedded_layout.run->lines().size(); index++) {
        // the run and the file shape the same glyphs at the same pen positions (§15, AD-34)
        const ShapedRun::Line& embedded_line = embedded_layout.run->lines()[index];
        const ShapedRun::Line& file_line = file_layout.run->lines()[index];
        // origin, glyphs and metrics are the whole of a layout, so each must match exactly (§15)
        EXPECT_EQ(embedded_line.origin, file_line.origin);
        EXPECT_EQ(embedded_line.glyphs, file_line.glyphs);
        EXPECT_EQ(embedded_line.width, file_line.width);
        EXPECT_EQ(embedded_line.ascent, file_line.ascent);
        EXPECT_EQ(embedded_line.descent, file_line.descent);
        // the byte span places the line in the text, and it matches for the same reason (§15)
        EXPECT_EQ(embedded_line.start_byte, file_line.start_byte);
        EXPECT_EQ(embedded_line.end_byte, file_line.end_byte);
    }
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

TEST(GlyphOutlineChecks, ACubicOutlineIsTessellated) {
    // arrange a cff flavored face, whose curves are cubics rather than the conics a truetype face reports (§6.4)
    std::ifstream probe("/System/Library/Fonts/Supplemental/STIXGeneral.otf", std::ios::binary);
    // a host without the face cannot exercise the cubic walk, which is reported rather than silently passing (§15)
    if (!probe.good())
        GTEST_SKIP() << "no cff conformance font on this host";
    // the engine is built over the face and the family the platform resolves for drawing (§9.1, AD-28)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create("/System/Library/Fonts/Supplemental/STIXGeneral.otf", "STIXGeneral");
    // a family the platform cannot resolve leaves no engine, which is reported rather than passing (§9.1)
    if (engine == nullptr)
        GTEST_SKIP() << "the cff conformance family did not resolve";
    const TextLayout layout = engine->measure("o", 0.0f, 100.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_GT(layout.run->glyph_count(), 0u);
    // act the round letter is walked, which is where a cubic is reported and flattened by the fixed segment count (§6.4)
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 100.0f);
    // assert the cubic path produced a tessellated shape rather than nothing, which pins the cubic callback (§6.4, §15)
    ASSERT_FALSE(outline.empty());
    EXPECT_GT(outline.size(), 32u);
}

TEST(EmbeddedTextDrawChecks, AnEmbeddedRunRecordsOneOutlinePerGlyph) {
    // arrange a run whose font the renderer cannot use, which is how an embedded run presents itself (§15, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Albor", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // act every glyph becomes an outline command at the pen position the run placed it (§6.4, AD-34)
    std::vector<DrawCommand> commands;
    for (const ShapedGlyph& glyph : layout.run->lines().front().glyphs) {
        // the outline is built once per glyph, which is what the cache would do in a frame (§6.4)
        const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(engine->font_hash(), glyph.id, 20.0f, engine->glyph_outline(glyph.id, 20.0f));
        // a glyph with no outline records nothing, which is what a space is (§6.2)
        if (outline->empty())
            continue;
        // the command lands at the glyph's own position in the run (§9.1)
        commands.push_back(DrawCommand::glyph_outline(outline, glyph.position, Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)}));
    }
    // assert the run produced commands, which is what stops it being a run that draws nothing (§15, AD-34)
    EXPECT_FALSE(commands.empty());
    EXPECT_EQ(commands.front().kind(), DrawCommandKind::GlyphOutline);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextDrawChecks, AnEmbeddedRunPutsInkOnTheReferenceSurface) {
    // arrange the same run, drawn onto the software reference rather than described (§15, AD-26)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Albor", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{160.0f, 60.0f}, 1.0f);
    SceneRecorder recorder(metrics);
    // act every glyph is recorded as an outline at the baseline the run placed it (§6.4, §9.1)
    for (const ShapedGlyph& glyph : layout.run->lines().front().glyphs) {
        // the outline is the embedded font's own contour, so this is the AD-34 path end to end (§15)
        const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(engine->font_hash(), glyph.id, 40.0f, engine->glyph_outline(glyph.id, 40.0f));
        // the baseline offset comes from the run's own line origin, which is what places the glyph (§9.1)
        recorder.glyph_outline(outline, Point{glyph.position.x, layout.run->lines().front().origin.y + glyph.position.y}, {.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
    }
    software::SoftwareRendererBackend backend;
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, *recorder.scene());
    const Screenshot drawn = capture_surface(*surface);
    // assert the surface carries ink, which is the claim: embedded text now reaches a raster (§15, AD-34)
    ASSERT_FALSE(drawn.empty());
    bool any_lit = false;
    for (std::uint8_t channel : drawn.pixels) {
        // one lit channel proves the outlines were rasterized rather than skipped (§15)
        if (channel != 0) {
            any_lit = true;
            break;
        }
    }
    EXPECT_TRUE(any_lit);
    // exit
    resources::FontRegistry::clear();
}

namespace
{
    // the node that draws one run through the ordinary text path, which is what the caller fix is about (§6.1)
    class EmbeddedTextNode final : public UiNode
    {
    public:
        // override: draw the run the engine measured, at the node's own origin (§6.1, §9.1)
        void render(RenderContext& context) override {
            // a run that measured nothing draws nothing, because it carries no glyph (§6.2)
            if (m_layout.run == nullptr)
                return;
            // this is the ordinary text call, with no special path for embedded runs (§6.1)
            context.draw_text(m_layout, Point{bounds().x, bounds().y}, {.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
            // the children draw over it, which is the composite contract of a node (§6.1)
            UiNode::render(context);
        }

        // the run this node draws, which the test measures with the vendored engine (§9.1)
        TextLayout m_layout;
    };
} // namespace

TEST(EmbeddedTextCallerChecks, AnEmbeddedRunDrawsThroughTheOrdinaryTextCall) {
    // arrange a build whose recorder can resolve outlines, which is what a composition root installs (§6.4, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    harfbuzz::EmbeddedGlyphProvider provider;
    SceneBuilder builder;
    // the resolver is installed, which is the one thing an application has to do (§6.4, AD-34)
    builder.set_outline_resolver(provider.resolver());
    // act a node draws vendored text through draw_text, exactly as any widget would (§6.1)
    EmbeddedTextNode node;
    node.m_layout = engine->measure("Albor", 0.0f, 40.0f);
    ASSERT_NE(node.m_layout.run, nullptr);
    // act the run is recorded through the normal build path, not through a special case (§6.1)
    const std::shared_ptr<const FrameScene> scene = builder.build(node, WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // assert the scene carries outline commands rather than a text command, which is the caller working (§6.4)
    bool any_outline = false;
    bool any_text = false;
    for (const DrawCommand& command : scene->commands()) {
        any_outline = any_outline || command.kind() == DrawCommandKind::GlyphOutline;
        any_text = any_text || command.kind() == DrawCommandKind::Text;
    }
    EXPECT_TRUE(any_outline);
    EXPECT_FALSE(any_text);
    // the run carries its font identity, which is what the recorder keys the expansion on (§15, AD-34)
    ASSERT_GT(node.m_layout.run->font_hash(), 0u);
    // assert the engine was built for that font, because the resolver is what reached into the build (§11.3)
    EXPECT_EQ(provider.engine_count(), 1u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextCallerChecks, AnEmbeddedRunPutsInkOnTheSurfaceThroughTheOrdinaryTextCall) {
    // arrange the same build, drawn onto the software reference (§15, AD-26)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    harfbuzz::EmbeddedGlyphProvider provider;
    SceneBuilder builder;
    builder.set_outline_resolver(provider.resolver());
    EmbeddedTextNode node;
    node.m_layout = engine->measure("Albor", 0.0f, 40.0f);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f);
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    software::SoftwareRendererBackend backend;
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, *scene);
    const Screenshot drawn = capture_surface(*surface);
    // assert the surface is not blank, which is the claim: embedded text draws with no special call (§15, AD-34)
    ASSERT_FALSE(drawn.empty());
    bool any_lit = false;
    for (std::uint8_t channel : drawn.pixels) {
        // one lit channel proves the outlines reached the raster (§15)
        if (channel != 0) {
            any_lit = true;
            break;
        }
    }
    EXPECT_TRUE(any_lit);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextCallerChecks, ARunTheRendererCanDrawStillGoesThroughTheTextCommand) {
    // arrange a run the renderer can draw, which is the path that must not change (§9.1, AD-28)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    SkiaTextEngine oracle;
    const TextLayout layout = oracle.measure("Albor", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert the run carries a registered identity, which is what lets a backend without the platform draw it (§15, AD-34)
    ASSERT_NE(layout.run->font_hash(), 0u);
    ASSERT_NE(resources::FontRegistry::find_hash(layout.run->font_hash()), nullptr);
    SceneBuilder builder;
    EmbeddedTextNode node;
    node.m_layout = layout;
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(node, WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // assert the text command is still what a drawable run produces, because the fix must not touch it (§6.4)
    bool any_text = false;
    for (const DrawCommand& command : scene->commands())
        any_text = any_text || command.kind() == DrawCommandKind::Text;
    EXPECT_TRUE(any_text);
}

TEST(EmbeddedTextCallerChecks, ARecorderWithoutAResolverDrawsNothingRatherThanSilently) {
    // arrange a vendored run recorded by a recorder that was never given a resolver (§6.4)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    // act the run is drawn with no resolver installed (§6.4)
    recorder.draw_text(engine->measure("Albor", 0.0f, 40.0f), Point{0.0f, 0.0f}, {.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
    // assert nothing was recorded, because a run drawn by no path is worse than one that is refused (§6.4)
    EXPECT_EQ(recorder.outline_command_count(), 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextCallerChecks, ThePipelineInstallsTheResolverAnApplicationGivesIt) {
    // arrange a pipeline, which is the composition root a frame is built through (§6.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    auto node = std::make_unique<EmbeddedTextNode>();
    node->m_layout = engine->measure("Albor", 0.0f, 40.0f);
    BindingEngine bindings;
    FramePipeline pipeline(bindings, std::move(node));
    harfbuzz::EmbeddedGlyphProvider provider;
    // act the application installs the resolver the one way it can (§6.4, AD-34)
    pipeline.set_outline_resolver(provider.resolver());
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    // assert a frame built through the pipeline carries outlines, which is the whole path proven (§6.1)
    ASSERT_NE(scene, nullptr);
    std::size_t outlines = 0;
    for (const DrawCommand& command : scene->commands())
        outlines = outlines + (command.kind() == DrawCommandKind::GlyphOutline ? 1u : 0u);
    EXPECT_GT(outlines, 0u);
    // exit
    resources::FontRegistry::clear();
}
