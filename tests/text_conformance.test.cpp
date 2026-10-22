#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/draw_command.h"
#include "render/line_batch.h"
#include "render/picture_resource.h"
#include "render/render-albor/albor_glyph_path.h"
#include "render/render-software/software_glyph_atlas.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/screenshot.h"
#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/embedded_glyph_provider.h"
#include "text/text-harfbuzz/harfbuzz_font_face.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "ui/frame_pipeline.h"
#include "ui/scene_builder.h"
#include "window/window_metrics.h"

using namespace albor;

namespace
{
    // the ofl font the vendored engine shapes from, which the tree carries so every host runs the suite (§9.1, AD-28, #92)
    const char* const FONT_PATH = ALBOR_TEST_FONT_PATH;

    // the family the file engine is built with, which names the carried face (§15, AD-28)
    const char* const FONT_FAMILY = ALBOR_TEST_FONT_FAMILY;

    // the ofl hebrew face the rtl checks shape with, which the tree carries beside the latin one (§9.2)
    const char* const RTL_FONT_PATH = ALBOR_TEST_RTL_FONT_PATH;

    // the family the carried rtl face is registered under, which is what a run names to reach it (§9.2, AD-34)
    const char* const RTL_FONT_FAMILY = ALBOR_TEST_RTL_FONT_FAMILY;

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
    // act assert the run is drawable and names the font it shaped from (§9.1, AD-34)
    EXPECT_TRUE(vendored->drawable());
    // the run draws through the bytes it carries, which the registry holds when no platform face exists (§15, AD-34)
    EXPECT_TRUE(resources::FontRegistry::find_hash(measured.run->font_hash()) != nullptr);
}

TEST(DualEngineConformanceChecks, AFileEngineNeedsNoPlatformFamily) {
    // arrange a family the platform does not have, which is the shape of an application shipping its own font (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    // act an engine is built over complete font bytes under that unknown family (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, "NoSuchFamilyAlbor");
    // assert the bytes are enough to shape and draw, because the run carries them rather than a typeface (§15, AD-34)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    EXPECT_TRUE(engine->drawable());
    // assert the bytes were registered, which is what lets a backend resolve the run by content (§15)
    const TextLayout measured = engine->measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(measured.run, nullptr);
    EXPECT_TRUE(resources::FontRegistry::find_hash(measured.run->font_hash()) != nullptr);
    // assert the run draws as outlines, because no platform face stands behind its family (§9.1, AD-34)
    EXPECT_EQ(measured.run->shape_source(), ShapeSource::Embedded);
    // exit
    resources::FontRegistry::clear();
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
    // arrange an engine over the carried font, which resolves a family through the registry (§9.1, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH, FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act
    const bool resolves = engine->resolves_font_selection();
    // assert the engine publishes that it resolves a family, so a caller may pass a selection (§9.1, AD-34)
    EXPECT_TRUE(resolves);
}

TEST(DualEngineConformanceChecks, ASelectionNamingNoCarriedFamilyIsRefused) {
    // arrange the carried fixture under one family, with no other family in the registry (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a selection names a family the build does not carry (§15, AD-34)
    const TextLayout layout = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborAbsentFamily"});
    // assert the run is refused rather than measured with the wrong face, which is what identity requires (§15, AD-34)
    EXPECT_EQ(layout.run, nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ASelectionNamingACarriedFamilyResolvesToIt) {
    // arrange the fixture carried under two names, which is how a family resolves through the registry (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborAlias", ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a run asks for the alias family, which is not the engine's own but resolves to the same bytes (§9.1, AD-34)
    const TextLayout plain = engine->measure("Albor", 0.0f, 16.0f);
    const TextLayout aliased = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborAlias"});
    // assert the alias resolved to the same carried bytes, so the layout is identical (§15, AD-34)
    ASSERT_NE(plain.run, nullptr);
    ASSERT_NE(aliased.run, nullptr);
    EXPECT_EQ(*plain.run, *aliased.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ASelectionChangesTheFaceTheRunShapesWith) {
    // arrange two different faces under two families, which a family selection must distinguish (§15, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no second conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborSecond", RTL_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same text is shaped once with the default face and once with the second family (§9.1, AD-34)
    const TextLayout first = engine->measure("Albor", 0.0f, 20.0f);
    const TextLayout second = engine->measure("Albor", 0.0f, 20.0f, FontSpec{.family = "AlborSecond"});
    // assert the two faces produce different runs, so the family truly selected a face (§15, AD-34)
    ASSERT_NE(first.run, nullptr);
    ASSERT_NE(second.run, nullptr);
    EXPECT_NE(*first.run, *second.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ASelectionFallsBackToACarriedFamily) {
    // arrange the engine's own family plus one other, with the named family absent (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborFallback", ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the named family is absent, so the fallback chain is what resolves the run (§9.1, AD-34)
    const TextLayout layout = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborAbsent", .fallback = {"AlborFallback"}});
    // assert the fallback family resolved, so the run is measured rather than refused (§15, AD-34)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_GT(layout.run->glyph_count(), 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ASelectionNamingTheEnginesOwnFamilyMeasuresPlainly) {
    // arrange the carried fixture, whose family the engine was built over (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the selection names the engine's own family, which uses the default face and skips the registry (§9.1)
    const TextLayout plain = engine->measure("Albor", 0.0f, 16.0f);
    const TextLayout named = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = std::string(ALBOR_TEST_FONT_FAMILY)});
    // assert the own family measures identically to no selection, which is the default face (§15, AD-34)
    ASSERT_NE(plain.run, nullptr);
    ASSERT_NE(named.run, nullptr);
    EXPECT_EQ(*plain.run, *named.run);
    // exit
    resources::FontRegistry::clear();
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
    const std::shared_ptr<const resources::EmbeddedFont> named = resources::FontRegistry::find_family(ALBOR_TEST_FONT_FAMILY);
    const std::shared_ptr<const resources::EmbeddedFont> alias = resources::FontRegistry::find_family("AlborFixtureAlias");
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
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 16.0f).points;
    EXPECT_FALSE(outline.empty());
    // exit
    resources::FontRegistry::clear();
}
TEST(EmbeddedFontChecks, ARegisteredFontIsFoundByFamilyAndByHash) {
    // arrange the registry starts empty, because a registration from another test would answer this (§2.2)
    resources::FontRegistry::clear();
    EXPECT_EQ(resources::FontRegistry::count(), 0u);
    // act a font is registered the way generated code registers one, over the carried bytes (§11.3, §15, AD-34)
    resources::FontRegistry::register_font(make_embedded_font("AlborTest", ALBOR_TEST_FONT_PATH));
    // assert both lookups answer, since a family is asked for and a hash is what a layout keys on (§15)
    ASSERT_TRUE(resources::FontRegistry::find_family("AlborTest") != nullptr);
    const std::shared_ptr<const resources::EmbeddedFont> found = resources::FontRegistry::find_family("AlborTest");
    EXPECT_TRUE(resources::FontRegistry::find_hash(found->content_hash) != nullptr);
    EXPECT_EQ(resources::FontRegistry::count(), 1u);
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
}

TEST(EmbeddedFontChecks, TheHashIsOverTheBytesAndNotTheName) {
    // arrange the same bytes registered under two names (§15, AD-34)
    resources::FontRegistry::clear();
    // the carried font is the fixture here, because the hash is over the bytes the build ships (§15, AD-34)
    resources::FontRegistry::register_font(make_embedded_font("AlborOne", ALBOR_TEST_FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborTwo", ALBOR_TEST_FONT_PATH));
    const std::shared_ptr<const resources::EmbeddedFont> one = resources::FontRegistry::find_family("AlborOne");
    const std::shared_ptr<const resources::EmbeddedFont> two = resources::FontRegistry::find_family("AlborTwo");
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
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 20.0f).points;
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
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 20.0f).points;
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
    const std::vector<Point> first = engine->glyph_outline(glyph, 20.0f).points;
    const std::vector<Point> second = engine->glyph_outline(glyph, 20.0f).points;
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
    const std::vector<Point> small = engine->glyph_outline(glyph, 20.0f).points;
    const std::vector<Point> large = engine->glyph_outline(glyph, 40.0f).points;
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

TEST(GlyphOutlineChecks, ACounterGlyphCarriesTwoClosedContours) {
    // arrange a round letter, whose counter is a second contour rather than the end of the first (§6.4)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("o", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the verb stream is counted, because a counter survives only when its contour is sealed (§6.4)
    const GlyphOutlineData walked = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 40.0f);
    std::size_t opened = 0;
    std::size_t sealed = 0;
    for (const PathVerb verb : walked.verbs) {
        // a move opens one contour of the glyph (§6.4)
        if (verb == PathVerb::MoveTo)
            opened++;
        // a close seals the contour it opened, which keeps the counter apart from the ring (§6.4)
        if (verb == PathVerb::Close)
            sealed++;
    }
    // assert the ring and its counter both reach the stream, which one flat point run cannot carry (§6.4)
    EXPECT_EQ(opened, 2u);
    EXPECT_EQ(sealed, 2u);
    // assert the stream consumes every point it carries, which a dropped verb would leave behind (§6.4)
    EXPECT_EQ(PathResource::point_demand(std::span<const PathVerb>(walked.verbs)), walked.points.size());
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AnOutlineIsHandedOverYDownTheBaseline) {
    // arrange a letter whose ink sits entirely above the baseline, which makes the sign of every y unambiguous (§9.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("x", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the vertical extent is measured against the baseline, which the walk puts at the origin (§9.1, AD-27)
    const GlyphOutlineData walked = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 40.0f);
    ASSERT_FALSE(walked.points.empty());
    float top = walked.points.front().y;
    float bottom = walked.points.front().y;
    for (const Point& point : walked.points) {
        // the top edge follows the smallest coordinate, which is where the x height lands (§9.1)
        top = std::min(top, point.y);
        // the bottom edge follows the largest, which stays at the baseline for a letter without a descender (§9.1)
        bottom = std::max(bottom, point.y);
    }
    // assert the x height rises above the baseline, which is negative in the y down space a surface draws in (§9.1)
    EXPECT_LT(top, -1.0f);
    // assert no ink falls below the baseline, which a y up walk would place there instead (§9.1)
    EXPECT_LT(bottom, 1.0f);
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
    const std::vector<Point> outline = engine->glyph_outline(1000000u, 20.0f).points;
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
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 100.0f).points;
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
    const std::vector<Point> outline = engine->glyph_outline(layout.run->lines().front().glyphs.front().id, 100.0f).points;
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

TEST(EmbeddedTextDrawChecks, AnEmbeddedCounterDrawsAsAHole) {
    // arrange a round letter drawn alone, so a painted counter cannot be mistaken for the ring (§6.4)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("o", 0.0f, 60.0f);
    ASSERT_NE(layout.run, nullptr);
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    const GlyphOutlineData walked = engine->glyph_outline(glyph, 60.0f);
    // act the counter's points are gathered, which is the contour after the second move (§6.4)
    std::vector<Point> counter;
    std::size_t cursor = 0;
    std::size_t contour = 0;
    for (const PathVerb verb : walked.verbs) {
        // a move opens the next contour and consumes the point it starts at (§6.4)
        if (verb == PathVerb::MoveTo) {
            contour++;
            cursor++;
            continue;
        }
        // a line consumes one point, which belongs to the counter while that contour is the second one (§6.4)
        if (verb == PathVerb::LineTo) {
            if (contour == 2)
                counter.push_back(walked.points[cursor]);
            cursor++;
        }
    }
    ASSERT_GT(counter.size(), 3u);
    // the centre of the round counter is the average of its points, which lands inside it (§6.4)
    float centre_x = 0.0f;
    float centre_y = 0.0f;
    for (const Point& point : counter) {
        centre_x += point.x;
        centre_y += point.y;
    }
    centre_x /= static_cast<float>(counter.size());
    centre_y /= static_cast<float>(counter.size());
    // the glyph is placed with its baseline below its x height, which keeps the whole letter on the surface (§9.1)
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{120.0f, 120.0f}, 1.0f);
    // the pen sits low enough that the letter clears the top edge and its counter stays on the surface (§9.1)
    const Point placed{40.0f, 80.0f};
    // the outline is retained once, which is what a frame replays (§6.4, AD-06)
    const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(engine->font_hash(), glyph, 60.0f, walked);
    // the frame clears to white, so an unfilled counter reads as background rather than as ink (§6.1)
    FrameScene scene(metrics, Color::rgb(1.0f, 1.0f, 1.0f));
    // the glyph is drawn as a filled outline, which is exactly what the replay records (§6.4)
    scene.add_command(DrawCommand::glyph_outline(outline, placed, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}));
    software::SoftwareRendererBackend backend;
    // the surface is the software reference, which is the screenshot oracle of the parity suite (§16, AD-26)
    const std::unique_ptr<RenderSurface> surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    // the frame is executed in one pass, which is what a composition root does (§6.1)
    backend.execute_frame(*surface, scene);
    const Screenshot drawn = capture_surface(*surface);
    ASSERT_FALSE(drawn.empty());
    // the sampled index needs the readback width, which is the physical extent and not the logical one (§6.3, AD-27)
    const std::size_t width = static_cast<std::size_t>(drawn.width);
    const std::size_t hole = (static_cast<std::size_t>(placed.y + centre_y) * width + static_cast<std::size_t>(placed.x + centre_x)) * 4u;
    ASSERT_LT(hole + 2u, drawn.pixels.size());
    // assert the counter stays background, which is the hole the second contour cuts (§6.4)
    EXPECT_EQ(drawn.pixels[hole], 255u);
    EXPECT_EQ(drawn.pixels[hole + 1], 255u);
    EXPECT_EQ(drawn.pixels[hole + 2], 255u);
    // act a point on the ring is sampled by stepping outward from the inner edge, which the stem spans (§6.4)
    const float outward_x = counter.front().x - centre_x;
    const float outward_y = counter.front().y - centre_y;
    const float outward_length = std::hypot(outward_x, outward_y);
    ASSERT_GT(outward_length, 1.0f);
    // the sample sits three pixels into the stem, which is narrower than the gap to the outer edge (§6.4)
    const float ring_x = counter.front().x + outward_x / outward_length * 3.0f;
    const float ring_y = counter.front().y + outward_y / outward_length * 3.0f;
    // the ring sample is checked for bounds before it is read, because the coordinates come from the font (§15)
    const std::size_t ring = (static_cast<std::size_t>(placed.y + ring_y) * width + static_cast<std::size_t>(placed.x + ring_x)) * 4u;
    ASSERT_LT(ring + 2u, drawn.pixels.size());
    // assert the ring carries ink beside the hole, which proves the letter drew rather than vanished (§6.2)
    EXPECT_LT(drawn.pixels[ring], 200u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextPlacementChecks, ScalesTheOutlineByTheTransformTheRecorderStandsAt) {
    // arrange a square outline, which is a glyph whose ink covers exactly its own corners (§6.4)
    const std::vector<PathVerb> verbs{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 0.0f}, Point{10.0f, 10.0f}, Point{0.0f, 10.0f}};
    const std::shared_ptr<const GlyphOutline> outline = std::make_shared<const GlyphOutline>(7u, 3u, 16.0f, GlyphOutlineData{.verbs = verbs, .points = points});
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f);
    // the pen is the same in both scenes, so only the recorder transform differs between them (§6.1)
    const Point pen{5.0f, 5.0f};
    SceneRecorder plain(metrics);
    // the frame clears to white, so the ink reads as the shape it is rather than as the background (§6.1)
    plain.set_background(Color::rgb(1.0f, 1.0f, 1.0f));
    plain.glyph_outline(outline, pen, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    SceneRecorder scaled(metrics);
    // the frame clears to white as well, which keeps the two frames comparable pixel for pixel (§6.1)
    scaled.set_background(Color::rgb(1.0f, 1.0f, 1.0f));
    scaled.save();
    // the scale is the transform a subtree carries into its children, which the glyph must inherit (§6.1, §6.4)
    scaled.set_transform(Transform::scaling(2.0f, 2.0f));
    scaled.glyph_outline(outline, pen, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    scaled.restore();
    // act both scenes draw onto their own reference surface, which is the oracle of the parity suite (§16, AD-26)
    software::SoftwareRendererBackend backend;
    const std::unique_ptr<RenderSurface> plain_surface = software::SoftwareRenderSurface::create(metrics);
    const std::unique_ptr<RenderSurface> scaled_surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(plain_surface, nullptr);
    ASSERT_NE(scaled_surface, nullptr);
    backend.execute_frame(*plain_surface, *plain.scene());
    backend.execute_frame(*scaled_surface, *scaled.scene());
    const Screenshot plain_drawn = capture_surface(*plain_surface);
    const Screenshot scaled_drawn = capture_surface(*scaled_surface);
    ASSERT_FALSE(plain_drawn.empty());
    ASSERT_FALSE(scaled_drawn.empty());
    // the lit channels are counted, because a doubling scale puts down four times the area (§6.4, §6.1)
    std::size_t plain_lit = 0u;
    std::size_t scaled_lit = 0u;
    for (std::size_t index = 0u; index + 2u < plain_drawn.pixels.size(); index += 4u) {
        // a lit channel in the plain frame is one pixel the unscaled glyph covered (§6.4)
        if (plain_drawn.pixels[index] != 255u)
            plain_lit++;
        // a lit channel in the scaled frame is one pixel the scaled glyph covered (§6.4)
        if (scaled_drawn.pixels[index] != 255u)
            scaled_lit++;
    }
    // assert the plain glyph put ink down at all, which is what makes the comparison meaningful (§6.4)
    ASSERT_GT(plain_lit, 0u);
    // assert the scaled glyph covers about four times the area, because the transform doubled both axes (§6.1)
    EXPECT_GT(scaled_lit, plain_lit * 3u);
    EXPECT_LT(scaled_lit, plain_lit * 5u);
    // the corner sample sits inside the unscaled square, which the scaled one has already left behind (§6.4)
    const std::size_t corner = (7u * static_cast<std::size_t>(plain_drawn.width) + 7u) * 4u;
    ASSERT_LT(corner + 2u, plain_drawn.pixels.size());
    EXPECT_LT(plain_drawn.pixels[corner], 200u);
    EXPECT_EQ(scaled_drawn.pixels[corner], 255u);
    // the far sample sits inside the scaled square only, which is where the transform put the ink (§6.1)
    const std::size_t far = (20u * static_cast<std::size_t>(scaled_drawn.width) + 20u) * 4u;
    ASSERT_LT(far + 2u, scaled_drawn.pixels.size());
    EXPECT_LT(scaled_drawn.pixels[far], 200u);
    EXPECT_EQ(plain_drawn.pixels[far], 255u);
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

TEST(EmbeddedTextCallerChecks, AnEmbeddedRunWalksItsOutlinesOnceAcrossFrames) {
    // arrange a run built twice, which is what two frames of the same text are (§6.1)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    harfbuzz::EmbeddedGlyphProvider provider;
    const GlyphOutlineResolver build_outline = provider.resolver();
    std::size_t resolved = 0u;
    SceneBuilder builder;
    // the resolver is counted, because not running it twice is the whole claim of a builder owned cache (§6.4, AD-06)
    builder.set_outline_resolver([&resolved, build_outline](const GlyphOutlineKey& key) {
        // every call is counted, because the cache is what has to stop a second one (§6.4, AD-06)
        resolved++;
        // the outline is built by the provider's own resolver, which is what the count wraps (§6.4)
        return build_outline(key);
    });
    EmbeddedTextNode node;
    node.m_layout = engine->measure("Albor", 0.0f, 40.0f);
    ASSERT_NE(node.m_layout.run, nullptr);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f);
    // act the same tree is built twice, which is the second frame of the same text (§6.1)
    const std::shared_ptr<const FrameScene> first = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(first, nullptr);
    const std::size_t after_first = resolved;
    const std::shared_ptr<const FrameScene> second = builder.build(node, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(second, nullptr);
    // assert the first build walked every visible glyph once, which are the misses the cache records (§6.4)
    EXPECT_EQ(after_first, node.m_layout.run->glyph_count());
    // assert the second build walked nothing, because the builder's cache answered the same keys (§6.4, AD-06)
    EXPECT_EQ(resolved, after_first);
    // assert both scenes carry the same commands, because a cached outline is the very same shape (§6.4)
    EXPECT_EQ(second->commands(), first->commands());
    EXPECT_GT(second->commands().size(), 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextCallerChecks, ARecorderWithoutACacheStillBuildsItsOutlines) {
    // arrange a recorder built directly, which is what a caller does when no builder borrows a cache (§6.4, AD-06)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    harfbuzz::EmbeddedGlyphProvider provider;
    const GlyphOutlineResolver build_outline = provider.resolver();
    std::size_t resolved = 0u;
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    // the resolver is counted with no cache borrowed, which is the contract the fallback branch keeps (§6.4, AD-06)
    recorder.set_outline_resolver([&resolved, build_outline](const GlyphOutlineKey& key) {
        // every call is counted, because a recorder with no cache builds every glyph itself (§6.4)
        resolved++;
        // the outline is built by the provider's own resolver, which is what the count wraps (§6.4)
        return build_outline(key);
    });
    // act an embedded run is drawn through the recorder directly (§6.1)
    const TextLayout layout = engine->measure("Albor", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    recorder.draw_text(layout, Point{.x = 0.0f, .y = 0.0f}, Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)});
    // assert every glyph was resolved, because nothing was borrowed to answer one (§6.4, AD-06)
    EXPECT_EQ(resolved, layout.run->glyph_count());
    EXPECT_EQ(recorder.outline_command_count(), layout.run->glyph_count());
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Albor", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert the run carries a registered identity, which is what lets a backend without the platform draw it (§15, AD-34)
    ASSERT_NE(layout.run->font_hash(), 0u);
    ASSERT_TRUE(resources::FontRegistry::find_hash(layout.run->font_hash()) != nullptr);
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
    // act the resolver the engine hands out is what the pipeline installs, which is the production wiring (§6.4, AD-34)
    pipeline.set_outline_resolver(engine->glyph_outline_resolver());
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

TEST(VendoredWrapChecks, AWrappedRunHasMoreThanOneRow) {
    // arrange the carried fixture, which needs no host font, shaped at a narrow ceiling (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a long sentence is measured against a ceiling far below its unbounded width (§8.2)
    const TextLayout layout = engine->measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    // assert the text wrapped into rows rather than one line (§8.2)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_GT(layout.run->lines().size(), 1u);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, TheByteSpansOfAWrappedRunRiseWithoutOverlapping) {
    // arrange the carried fixture, which the wrap walks the same way the skia oracle does (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const std::string text = "the quick brown fox jumps over the lazy dog";
    // act
    const TextLayout layout = engine->measure(text, 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert the rows tile the text without overlapping and end at its end (§9.1)
    std::size_t previous_end = 0;
    for (const ShapedRun::Line& line : layout.run->lines()) {
        // each row starts exactly where the previous row ended, so the rows tile the text (§9.1)
        EXPECT_EQ(line.start_byte, previous_end);
        // a row always spans at least its break, so it is never empty of bytes (§9.1)
        EXPECT_GT(line.end_byte, line.start_byte);
        // the next row compares against this row's end (§9.1)
        previous_end = line.end_byte;
    }
    // the last row ends where the text ends, so nothing is dropped (§9.1)
    EXPECT_EQ(previous_end, text.size());
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, EveryWrappedRowCarriesItsOwnBaseline) {
    // arrange the carried fixture, whose rows each place their own baseline (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act
    const TextLayout layout = engine->measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert each row's baseline sits below the previous one, which is what stops rows overprinting (§8.2)
    float previous_baseline = -1.0f;
    for (const ShapedRun::Line& line : layout.run->lines()) {
        // a row's baseline is strictly below the previous row's, so the rows stack down the run (§8.2)
        EXPECT_GT(line.origin.y, previous_baseline);
        // the next row compares against this row's baseline (§8.2)
        previous_baseline = line.origin.y;
    }
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, AnUnboundedVendoredRunStaysOnOneLine) {
    // arrange the carried fixture, measured with no ceiling so nothing forces a break (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act
    const TextLayout layout = engine->measure("the quick brown fox jumps over the lazy dog", 0.0f, 20.0f);
    // assert an unbounded run is one line, which is what a call site that passes no width expects (§8.2)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_EQ(layout.run->lines().size(), 1u);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, AnExplicitNewlineStartsANewRow) {
    // arrange the carried fixture, measured against a ceiling that also bounds the rows (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a mandatory break splits the run even though the text is short (§8.2)
    const TextLayout layout = engine->measure("first\nsecond", 80.0f, 20.0f);
    // assert the two lines became two rows, because a newline is a mandatory break (§8.2)
    ASSERT_NE(layout.run, nullptr);
    ASSERT_EQ(layout.run->lines().size(), 2u);
    // assert the first row ends at the break and the second starts there (§9.1)
    EXPECT_EQ(layout.run->lines()[0].start_byte, 0u);
    EXPECT_EQ(layout.run->lines()[0].end_byte, 6u);
    EXPECT_EQ(layout.run->lines()[1].start_byte, 6u);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, AHardBreakIsNotShapedAsAGlyph) {
    // arrange the carried fixture, whose hard break must never become a tofu box in the run (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a run ending in a newline is measured, where the newline is a break and not content (§8.2)
    const TextLayout layout = engine->measure("first\n", 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_FALSE(layout.run->lines().empty());
    // assert the row's glyphs cover only the five letters, because the newline is not shaped (§9.1)
    const ShapedRun::Line& first = layout.run->lines().front();
    EXPECT_EQ(first.start_byte, 0u);
    EXPECT_EQ(first.end_byte, 6u);
    // the word "first" carries exactly five glyphs, so no glyph stands for the break (§9.1)
    EXPECT_EQ(first.glyphs.size(), 5u);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, AConsecutiveBreakLeavesABlankRow) {
    // arrange the carried fixture, whose two adjacent newlines must leave an empty row between (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a run with a blank line between two letters is measured (§8.2)
    const TextLayout layout = engine->measure("a\n\nb", 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert three rows, where the middle one is blank because the two breaks meet (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 3u);
    EXPECT_TRUE(layout.run->lines()[1].glyphs.empty());
    // the blank row still carries a baseline below the first, which gives it a height (§9.1)
    EXPECT_GT(layout.run->lines()[1].origin.y, layout.run->lines()[0].origin.y);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, EveryWrappedRowHonoursTheCeiling) {
    // arrange the carried fixture measured against a ceiling the words must fit under (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a sentence of short words is wrapped, so every row can fit the ceiling (§8.2)
    const TextLayout layout = engine->measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert no row exceeds the ceiling, because a wrap that ignored it would still split the text (§8.2)
    for (const ShapedRun::Line& line : layout.run->lines())
        EXPECT_LE(line.width, 80.0f);
    // exit
    resources::FontRegistry::clear();
}

TEST(VendoredWrapChecks, AnUnboundedRunStillBreaksAtANewline) {
    // arrange the carried fixture measured with no ceiling, where only a mandatory break can end a row (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a run with one newline is measured unbounded (§8.2)
    const TextLayout layout = engine->measure("first\nsecond", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert the newline still splits the run, because a hard break does not need a ceiling (§9.1)
    EXPECT_EQ(layout.run->lines().size(), 2u);
    // exit
    resources::FontRegistry::clear();
}
TEST(TextEngineInstallChecks, TheInstalledFactoryBuildsTheEmbeddedEngine) {
    // arrange the tree carries a font under a family the composition root names (§11.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    set_installed_font_family(std::string(ALBOR_TEST_FONT_FAMILY));
    // act installing the vendored engine is what linking it selects, and the factory then builds it (§9.1)
    harfbuzz::install_harfbuzz_text_engine();
    const std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert the factory built an engine over the carried font, with no platform font needed (§15, AD-34)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    const TextLayout layout = engine->measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(layout.run, nullptr);
    EXPECT_GT(layout.run->glyph_count(), 0u);
    // exit clear the factory, the family and the registry, so one test's install cannot answer another (§2.2)
    set_text_engine_factory(nullptr);
    // the family is cleared beside the factory, because both are global state a later test would read (§2.2)
    set_installed_font_family("");
    // the registry is emptied for the same reason (§2.2)
    resources::FontRegistry::clear();
}

TEST(TextEngineInstallChecks, TheDefaultFamilyIsUsedWhenNoneIsNamed) {
    // arrange the registry carries the carried font under the default family the framework names (§11.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(std::string(TextEngine::DEFAULT_FONT_FAMILY), ALBOR_TEST_FONT_PATH));
    set_installed_font_family("");
    // act installing builds the engine over the family the composition root left unnamed (§9.1)
    harfbuzz::install_harfbuzz_text_engine();
    const std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert the unnamed family fell back to the default, which the registry carried (§11.3, AD-34)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    // exit
    set_text_engine_factory(nullptr);
    // the family is cleared beside the factory and the registry, so no global leaks into a later test (§2.2)
    set_installed_font_family("");
    resources::FontRegistry::clear();
}

TEST(TextEngineInstallChecks, TheFactoryIsEmptyUntilTheEngineIsInstalled) {
    // arrange no backend is installed, which is the state a process starts in (§9.1)
    set_text_engine_factory(nullptr);
    // act
    const std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert an uninstalled factory builds nothing, so linking the engine is the whole selection (§9.1)
    EXPECT_EQ(engine, nullptr);
}

TEST(GoldenCorpusChecks, TheVendoredLayoutMatchesTheCheckedInCorpus) {
    // arrange the carried fixture, whose layout is a total function of its inputs, so it is pinned exactly (§15)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // one expected glyph, which carries the id, pen position and source byte the shape must produce (§9.1)
    struct GoldenGlyph
    {
        // the glyph index inside the font (§9.1)
        std::uint32_t id;
        // the pen position relative to the row baseline, in logical pixels (§9.1, AD-27)
        float x;
        float y;
        // the byte offset the glyph came from, which a caret resolves against (§9.1)
        std::uint32_t cluster;
    };
    // one corpus entry, whose every value was recorded from the engine at twenty pixels and carries no tolerance (§15)
    struct GoldenEntry
    {
        // the string the entry shapes at twenty logical pixels (§15, AD-27)
        const char* text;
        // the advance, ascent and descent the run must report (§9.1)
        float width;
        float ascent;
        float descent;
        // each glyph's id, pen position and source byte, which together are the layout (§9.1)
        std::vector<GoldenGlyph> glyphs;
    };
    const std::vector<GoldenEntry> corpus = {
        {.text = "A", .width = 13.796875f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{2, 0.0f, 0.0f, 0}}}, {.text = "Albor", .width = 50.421875f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{2, 0.0f, 0.0f, 0}, {736, 13.796875f, 0.0f, 1}, {578, 18.640625f, 0.0f, 2}, {790, 30.890625f, 0.0f, 3}, {852, 42.890625f, 0.0f, 4}}}, {.text = "Hello", .width = 48.203125f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{161, 0.0f, 0.0f, 0}, {614, 14.859375f, 0.0f, 1}, {736, 26.515625f, 0.0f, 2}, {736, 31.359375f, 0.0f, 3}, {790, 36.203125f, 0.0f, 4}}}, {.text = "Wavy", .width = 52.09375f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{459, 0.0f, 0.0f, 0}, {507, 18.6875f, 0.0f, 1}, {980, 29.625f, 0.0f, 2}, {998, 40.859375f, 0.0f, 3}}}, {.text = "Ti", .width = 17.75f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{411, 0.0f, 0.0f, 0}, {689, 12.90625f, 0.0f, 1}}}, {.text = "caf\u00E9", .width = 41.34375f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{586, 0.0f, 0.0f, 0}, {507, 11.625f, 0.0f, 1}, {647, 22.859375f, 0.0f, 2}, {618, 29.6875f, 0.0f, 3}}}, {.text = "e\u0301", .width = 11.65625f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{618, 0.0f, 0.0f, 0}}}, {.text = "A\u0300B", .width = 26.890625f, .ascent = 19.375f, .descent = 4.82421875f, .glyphs = {{5, 0.0f, 0.0f, 0}, {65, 13.796875f, 0.0f, 3}}},
    };
    for (const GoldenEntry& entry : corpus) {
        // act the entry is measured at the size the corpus was recorded at (§15)
        const TextLayout layout = engine->measure(entry.text, 0.0f, 20.0f);
        // assert the run carries the identity a renderer keys on, which is part of the recorded layout (§15, AD-34)
        ASSERT_NE(layout.run, nullptr);
        EXPECT_EQ(layout.run->font_hash(), engine->font_hash());
        EXPECT_EQ(layout.run->shape_source(), ShapeSource::Embedded);
        EXPECT_EQ(layout.run->font_size(), 20.0f);
        // assert the metrics match the checked-in values exactly, because layout identity carries no tolerance (§15)
        EXPECT_EQ(layout.size.width, entry.width);
        EXPECT_EQ(layout.ascent, entry.ascent);
        EXPECT_EQ(layout.descent, entry.descent);
        const std::vector<ShapedGlyph>& shaped = layout.run->lines().front().glyphs;
        ASSERT_EQ(shaped.size(), entry.glyphs.size());
        for (std::size_t index = 0; index < shaped.size(); index++) {
            // each glyph's id, pen position and source byte are the layout, so each matches exactly (§9.1, §15)
            EXPECT_EQ(shaped[index].id, entry.glyphs[index].id);
            EXPECT_EQ(shaped[index].position.x, entry.glyphs[index].x);
            EXPECT_EQ(shaped[index].position.y, entry.glyphs[index].y);
            EXPECT_EQ(shaped[index].cluster, entry.glyphs[index].cluster);
        }
    }
    // exit
    resources::FontRegistry::clear();
}

TEST(GoldenCorpusChecks, TheVendoredLayoutIsDeterministic) {
    // arrange the carried fixture measured twice over the same inputs (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same run is measured twice, which a total function of its inputs must answer identically (§15)
    const TextLayout first = engine->measure("Albor framework", 0.0f, 20.0f);
    const TextLayout second = engine->measure("Albor framework", 0.0f, 20.0f);
    // assert the two runs are equal field for field, which a total function of its inputs must answer (§15)
    ASSERT_NE(first.run, nullptr);
    ASSERT_NE(second.run, nullptr);
    EXPECT_EQ(*first.run, *second.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(GoldenCorpusChecks, TheWrappedLayoutMatchesTheCheckedInCorpus) {
    // arrange the carried fixture wrapped against a fixed ceiling, which no exact test pinned before (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the paragraph is measured against the ceiling the corpus was recorded at (§8.2)
    const TextLayout layout = engine->measure("the quick brown fox", 80.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // assert the run packs the text into the recorded rows, which are the whole wrapped layout (§15)
    EXPECT_EQ(layout.run->lines().size(), 4u);
    EXPECT_EQ(layout.size.width, 59.28125f);
    EXPECT_EQ(layout.size.height, 96.796875f);
    // the rows, each with the baseline it is drawn at and the byte span it covers (§9.1)
    const struct
    {
        // the baseline offset from the run origin, which stacks the rows (§8.2)
        float baseline;
        std::size_t start;
        std::size_t end;
        // the number of glyphs the row draws, which drops the break byte (§9.1)
        std::size_t glyphs;
    } rows[] = {
        {.baseline = 19.375f, .start = 0, .end = 4, .glyphs = 3},
        {.baseline = 43.57421875f, .start = 4, .end = 10, .glyphs = 5},
        {.baseline = 67.7734375f, .start = 10, .end = 16, .glyphs = 5},
        {.baseline = 91.97265625f, .start = 16, .end = 19, .glyphs = 3},
    };
    for (std::size_t index = 0; index < layout.run->lines().size(); index++) {
        // each row's baseline, byte span and glyph count match the recorded row exactly (§15)
        EXPECT_EQ(layout.run->lines()[index].origin.y, rows[index].baseline);
        EXPECT_EQ(layout.run->lines()[index].start_byte, rows[index].start);
        EXPECT_EQ(layout.run->lines()[index].end_byte, rows[index].end);
        EXPECT_EQ(layout.run->lines()[index].glyphs.size(), rows[index].glyphs);
    }
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, AnEmbeddedEngineIsDrawableWithoutATypeface) {
    // arrange the carried fixture, whose engine draws outlines and owns no platform typeface (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act, assert an embedded engine is drawable through its outlines without a typeface (§15, AD-34)
    EXPECT_TRUE(engine->drawable());
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ASecondSelectionOfOneFamilyAnswersFromTheCache) {
    // arrange the fixture carried under a second family, which is resolved on its first use (§15, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    resources::FontRegistry::register_font(make_embedded_font("AlborCached", ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same foreign family is measured twice, so the second answers from the cache (§9.1)
    const TextLayout first = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborCached"});
    const TextLayout second = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborCached"});
    // assert both resolved to the same carried bytes, which the cache preserves (§15, AD-34)
    ASSERT_NE(first.run, nullptr);
    ASSERT_NE(second.run, nullptr);
    EXPECT_EQ(*first.run, *second.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(DualEngineConformanceChecks, ACorruptCarriedFamilyIsSkippedRatherThanCrashing) {
    // arrange a family whose carried bytes name no face, which the registry still accepts (§9.1)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = "AlborCorrupt", .content_hash = 0, .bytes = {1, 2, 3, 4, 5, 6, 7, 8}});
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the corrupt family is named once with a fallback and once alone, which reads the cached empty face (§9.1)
    const TextLayout with_fallback = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborCorrupt", .fallback = {std::string(ALBOR_TEST_FONT_FAMILY)}});
    const TextLayout alone = engine->measure("Albor", 0.0f, 16.0f, FontSpec{.family = "AlborCorrupt"});
    // assert the corrupt family is skipped so the fallback shapes, and a lone corrupt family is refused (§15, AD-34)
    ASSERT_NE(with_fallback.run, nullptr);
    EXPECT_EQ(alone.run, nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(EmbeddedTextDrawChecks, TheEngineProvidesTheOutlineResolver) {
    // arrange the carried fixture, whose engine draws outlines and provides the resolver (§6.4, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the resolver the engine hands out is asked for the glyph the run carries (§6.4)
    const GlyphOutlineResolver resolver = engine->glyph_outline_resolver();
    ASSERT_TRUE(static_cast<bool>(resolver));
    const std::shared_ptr<const GlyphOutline> outline = resolver(GlyphOutlineKey{.font_hash = layout.run->font_hash(), .glyph_id = layout.run->lines().front().glyphs.front().id, .font_size = 20.0f});
    // assert the resolver walked the carried font into a real outline, which stage two replays (§6.4, §15)
    ASSERT_NE(outline, nullptr);
    EXPECT_FALSE(outline->empty());
    // exit
    resources::FontRegistry::clear();
}
TEST(GlyphOutlineChecks, TheOutlinePairsItsVerbsWithThePointsOfEveryContour) {
    // arrange the carried fixture, whose glyph is walked into closed contours of points (§6.4, AD-34)
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(FONT_FAMILY, FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    const GlyphOutlineData walked = engine->glyph_outline(glyph, 20.0f);
    ASSERT_FALSE(walked.points.empty());
    // act the outline is built once over the walked contours, which pairs the verbs with their points (§6.4)
    const GlyphOutline outline(engine->font_hash(), glyph, 20.0f, walked);
    // assert the stream consumes exactly the points it carries, which a dropped verb would break (§6.4)
    EXPECT_EQ(PathResource::point_demand(std::span<const PathVerb>(outline.verbs())), outline.points().size());
    // assert the stream opens with a move and seals with a close, which is the ring a glyph draws (§6.4)
    EXPECT_EQ(outline.verbs().front(), PathVerb::MoveTo);
    EXPECT_EQ(outline.verbs().back(), PathVerb::Close);
    // assert the retained path carries the same stream, which is what a frame draws without allocating (§6.4)
    ASSERT_NE(outline.path(), nullptr);
    EXPECT_EQ(outline.path()->verbs().size(), outline.verbs().size());
    EXPECT_EQ(outline.path()->points().size(), outline.points().size());
    // exit
    resources::FontRegistry::clear();
}

TEST(GlyphOutlineChecks, AnOutlineKeepsTheContoursItWasBuiltFrom) {
    // arrange a ring with a counter, which is two contours rather than one point run (§6.4)
    const GlyphOutlineData ring{
        .verbs = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close, PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close},
        .points = {Point{0.0f, 0.0f}, Point{8.0f, 0.0f}, Point{4.0f, 8.0f}, Point{2.0f, 2.0f}, Point{3.0f, 6.0f}, Point{4.0f, 2.0f}},
    };
    // act
    const GlyphOutline outline(7u, 1u, 10.0f, ring);
    // assert the verbs are handed back as built, because a close and a move are what keep the counter apart (§6.4)
    ASSERT_EQ(outline.verbs().size(), ring.verbs.size());
    ASSERT_EQ(outline.points().size(), ring.points.size());
    EXPECT_EQ(outline.verbs()[3], PathVerb::Close);
    EXPECT_EQ(outline.verbs()[4], PathVerb::MoveTo);
    // assert the retained path carries the same stream, which the replay draws directly (§6.4)
    ASSERT_NE(outline.path(), nullptr);
    EXPECT_EQ(outline.path()->verbs().size(), ring.verbs.size());
    EXPECT_EQ(outline.path()->points().size(), ring.points.size());
    // assert an outline with no contour is empty, which is the one case the replay skips (§6.2)
    const GlyphOutline none(7u, 2u, 10.0f, GlyphOutlineData{});
    EXPECT_TRUE(none.empty());
    // assert a stream that asks for more points than it carries is refused, which keeps the replay in bounds (§6.4)
    const GlyphOutline mismatched(7u, 3u, 10.0f, GlyphOutlineData{.verbs = ring.verbs, .points = {Point{0.0f, 0.0f}}});
    EXPECT_TRUE(mismatched.empty());
    // assert the path identity carries the size, because one glyph at two sizes is two different shapes (§6.4, AD-27)
    const GlyphOutline same_size(7u, 1u, 10.0f, ring);
    const GlyphOutline larger(7u, 1u, 20.0f, ring);
    EXPECT_EQ(same_size.path()->id(), outline.path()->id());
    EXPECT_NE(larger.path()->id(), outline.path()->id());
}

TEST(RtlChecks, AnRtlRunShapesInTheDeclaredDirection) {
    // arrange a host hebrew face, which is the only way to shape a real rtl script on this build (§9.2)
    std::ifstream probe(RTL_FONT_PATH, std::ios::binary);
    // a host without an rtl face cannot exercise the direction, which is reported rather than passing (§9.2)
    if (!probe.good())
        GTEST_SKIP() << "no rtl font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(RTL_FONT_FAMILY, RTL_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(RTL_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const std::string text = "\u05E9\u05DC\u05D5\u05DD";
    // act the same text is shaped in each declared direction (§9.2)
    const TextLayout rtl = engine->measure(text, 0.0f, 20.0f, FontSpec{}, TextDirection::Rtl);
    const TextLayout ltr = engine->measure(text, 0.0f, 20.0f, FontSpec{}, TextDirection::Ltr);
    // assert the rtl run shaped real glyphs, so the face covers hebrew (§9.2)
    ASSERT_NE(rtl.run, nullptr);
    ASSERT_NE(ltr.run, nullptr);
    ASSERT_GE(rtl.run->lines().front().glyphs.size(), 2u);
    EXPECT_NE(rtl.run->lines().front().glyphs.front().id, 0u);
    // assert the run is visual order, which its descending clusters prove rather than merely differing (§9.2)
    EXPECT_GT(rtl.run->lines().front().glyphs.front().cluster, rtl.run->lines().front().glyphs.back().cluster);
    // assert a declared direction changed the run from the content-shaped one, which the ltr run is (§9.2)
    EXPECT_NE(*rtl.run, *ltr.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(RtlChecks, AnInheritedDirectionShapesTheScriptsOwnWay) {
    // arrange a host hebrew face, whose script is what an inherited direction defers to (§9.2)
    std::ifstream probe(RTL_FONT_PATH, std::ios::binary);
    // a host without an rtl face cannot exercise the direction, which is reported rather than passing (§9.2)
    if (!probe.good())
        GTEST_SKIP() << "no rtl font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(RTL_FONT_FAMILY, RTL_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(RTL_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const std::string text = "\u05E9\u05DC\u05D5\u05DD";
    // act the same text is shaped with an inherited direction and with an explicit rtl one (§9.2)
    const TextLayout inherited = engine->measure(text, 0.0f, 20.0f, FontSpec{}, TextDirection::Inherit);
    const TextLayout rtl = engine->measure(text, 0.0f, 20.0f, FontSpec{}, TextDirection::Rtl);
    // assert the inherited run already shaped right to left, so the default did not regress the script (§9.2)
    ASSERT_NE(inherited.run, nullptr);
    ASSERT_NE(rtl.run, nullptr);
    ASSERT_GE(inherited.run->lines().front().glyphs.size(), 2u);
    EXPECT_GT(inherited.run->lines().front().glyphs.front().cluster, inherited.run->lines().front().glyphs.back().cluster);
    // assert inheriting the script's direction and declaring it agree, because the content chose the same (§9.2)
    EXPECT_EQ(*inherited.run, *rtl.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(RtlChecks, AnRtlRunIsReproducible) {
    // arrange the host hebrew face, shaped twice in the same declared direction (§9.2)
    std::ifstream probe(RTL_FONT_PATH, std::ios::binary);
    // a host without an rtl face cannot exercise the direction, which is reported rather than passing (§9.2)
    if (!probe.good())
        GTEST_SKIP() << "no rtl font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(RTL_FONT_FAMILY, RTL_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(RTL_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same direction is asked for twice, which a total function of its inputs answers identically (§15)
    const TextLayout first = engine->measure("\u05D0\u05D1\u05D2", 0.0f, 20.0f, FontSpec{}, TextDirection::Rtl);
    const TextLayout second = engine->measure("\u05D0\u05D1\u05D2", 0.0f, 20.0f, FontSpec{}, TextDirection::Rtl);
    // assert the two runs are equal, so direction does not make the layout jitter (§15)
    ASSERT_NE(first.run, nullptr);
    ASSERT_NE(second.run, nullptr);
    EXPECT_EQ(*first.run, *second.run);
    // exit
    resources::FontRegistry::clear();
}

TEST(RtlChecks, AWrappedRtlRunKeepsItsDirectionOnEveryRow) {
    // arrange the host hebrew face, wrapped against a ceiling that forces several rows (§9.2)
    std::ifstream probe(RTL_FONT_PATH, std::ios::binary);
    // a host without an rtl face cannot exercise the direction, which is reported rather than passing (§9.2)
    if (!probe.good())
        GTEST_SKIP() << "no rtl font on this host";
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(RTL_FONT_FAMILY, RTL_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(RTL_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act a hebrew paragraph is wrapped in a declared rtl direction (§8.2, §9.2)
    const TextLayout layout = engine->measure("\u05E9\u05DC\u05D5\u05DD \u05E2\u05D5\u05DC\u05DD \u05E9\u05DC\u05D5\u05DD \u05E2\u05D5\u05DC\u05DD", 60.0f, 20.0f, FontSpec{}, TextDirection::Rtl);
    // assert the paragraph wrapped and every row kept the declared direction (§9.2)
    ASSERT_NE(layout.run, nullptr);
    ASSERT_GT(layout.run->lines().size(), 1u);
    for (const ShapedRun::Line& line : layout.run->lines()) {
        // a row with fewer than two glyphs cannot show an order, so only a longer row is checked (§9.2)
        if (line.glyphs.size() < 2)
            continue;
        // the row is in visual order, which its descending clusters prove (§9.2)
        EXPECT_GT(line.glyphs.front().cluster, line.glyphs.back().cluster);
    }
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareGlyphAtlasChecks, TheAtlasRasterizesAGlyphFromTheCarriedBytes) {
    // arrange the carried fixture registered under its hash, which is what the atlas resolves (§6.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(font, nullptr);
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 20.0f);
    ASSERT_NE(layout.run, nullptr);
    ASSERT_FALSE(layout.run->lines().empty());
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    // act the atlas rasterizes the glyph from the carried bytes, which is the reference's own rasterizer (§6.3, AD-26)
    software::SoftwareGlyphAtlas atlas;
    const std::shared_ptr<const software::GlyphBitmap> bitmap = atlas.glyph(font->content_hash, glyph, 20.0f);
    // assert the atlas produced a real coverage, so the reference rasterizes rather than asking skia (§6.3)
    ASSERT_NE(bitmap, nullptr);
    EXPECT_GT(bitmap->width, 0u);
    EXPECT_GT(bitmap->height, 0u);
    EXPECT_EQ(bitmap->coverage.size(), static_cast<std::size_t>(bitmap->width) * static_cast<std::size_t>(bitmap->height));
    // assert the top pad row is transparent, which is what keeps the rounding from clipping the glyph (§6.3)
    bool top_empty = true;
    for (unsigned int col = 0; col < bitmap->width; col++)
        top_empty = top_empty && bitmap->coverage[col] == 0;
    EXPECT_TRUE(top_empty);
    // the lowest inked row is found, because an A stands on two feet and a sheared raster does not (§6.3)
    unsigned int lowest = bitmap->height;
    for (unsigned int row = 0; row < bitmap->height; row++) {
        for (unsigned int col = 0; col < bitmap->width; col++) {
            const bool inked = bitmap->coverage[static_cast<std::size_t>(row) * bitmap->width + col] > 0;
            if (inked)
                lowest = row;
        }
    }
    ASSERT_LT(lowest, bitmap->height);
    // both feet carry ink, which a wrongly scaled or strided raster would not (§6.3, §15)
    bool left_foot = false;
    bool right_foot = false;
    for (unsigned int col = 0; col < bitmap->width; col++) {
        const bool inked = bitmap->coverage[static_cast<std::size_t>(lowest) * bitmap->width + col] > 0;
        left_foot = left_foot || (inked && col < bitmap->width / 3u);
        right_foot = right_foot || (inked && col >= (bitmap->width * 2u) / 3u);
    }
    EXPECT_TRUE(left_foot);
    EXPECT_TRUE(right_foot);
    // assert a second request answers from the cache, which is what makes the atlas cheap (§6.3)
    const std::shared_ptr<const software::GlyphBitmap> again = atlas.glyph(font->content_hash, glyph, 20.0f);
    EXPECT_EQ(again, bitmap);
    EXPECT_EQ(atlas.count(), 1u);
    // assert a smaller size also rasterizes ink, which the pad on the bounds makes safe from rounding (§6.3)
    const std::shared_ptr<const software::GlyphBitmap> small = atlas.glyph(font->content_hash, glyph, 16.0f);
    ASSERT_NE(small, nullptr);
    EXPECT_EQ(atlas.count(), 2u);
    // assert a size of no pixels and a hash the build does not carry are each refused (§6.3)
    EXPECT_EQ(atlas.glyph(font->content_hash, glyph, 0.0f), nullptr);
    EXPECT_EQ(atlas.glyph(1u, glyph, 20.0f), nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareTextChecks, TheSoftwareSurfaceDrawsTextThroughItsOwnAtlas) {
    // arrange the carried fixture and an embedded run, which the atlas rasterizes by hash (§6.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("Albor", 0.0f, 32.0f);
    ASSERT_NE(layout.run, nullptr);
    // act the run is drawn on the software reference, whose context uses its own atlas (§6.3, AD-26)
    const std::unique_ptr<software::SoftwareRenderSurface> surface = software::SoftwareRenderSurface::create(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    context.draw_text(layout, Point{10.0f, 10.0f}, Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the reference put ink down where the run was placed, which is the atlas reaching the raster (§6.3)
    std::size_t inked = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        inked = inked + (pixels[index + 3] > 0u ? 1u : 0u);
    EXPECT_GT(inked, 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareTextChecks, ARunTheAtlasCannotResolveFallsBackToSkia) {
    // arrange a run whose font hash the build does not carry, which the atlas cannot rasterize (§6.3)
    ShapedRun::Line line;
    line.glyphs.push_back(ShapedGlyph{.id = 1u, .position = Point{0.0f, 0.0f}, .cluster = 0u});
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f, 999999u, ShapeSource::Embedded);
    const TextLayout layout{.run = run, .size = Size{20.0f, 20.0f}, .ascent = 16.0f, .descent = 4.0f};
    // act the run is drawn, and the atlas refuses it, so skia draws the fallback rather than dropping the text (§6.3)
    const std::unique_ptr<software::SoftwareRenderSurface> surface = software::SoftwareRenderSurface::create(WindowMetrics::from_logical(Size{80.0f, 40.0f}, 1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    context.draw_text(layout, Point{4.0f, 4.0f}, Paint{.fill = Color::rgba(1.0f, 1.0f, 1.0f, 1.0f)});
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the frame finished, which is what the fallback guarantees instead of dropping the run (§6.3)
    EXPECT_FALSE(pixels.empty());
}

TEST(SoftwareGlyphAtlasChecks, TheAtlasRefusesGlyphsItCannotRasterize) {
    // arrange the carried fixture, registered under its hash (§6.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(font, nullptr);
    software::SoftwareGlyphAtlas atlas;
    // act, assert an out-of-range glyph has no extents, so it is refused (§6.3)
    EXPECT_EQ(atlas.glyph(font->content_hash, 1000000u, 20.0f), nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareGlyphAtlasChecks, TheAtlasRefusesACorruptCarriedFont) {
    // arrange a family whose bytes name no face, which the registry still accepts (§9.1)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = "AlborCorrupt", .content_hash = 0, .bytes = {1, 2, 3, 4, 5, 6, 7, 8}});
    const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family("AlborCorrupt");
    ASSERT_NE(font, nullptr);
    software::SoftwareGlyphAtlas atlas;
    // act, assert a corrupt face is refused rather than rasterized (§6.3)
    EXPECT_EQ(atlas.glyph(font->content_hash, 1u, 20.0f), nullptr);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareGlyphAtlasChecks, ClearingTheAtlasDropsItsBitmaps) {
    // arrange a rasterized glyph, which the atlas caches (§6.3)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(font, nullptr);
    software::SoftwareGlyphAtlas atlas;
    ASSERT_NE(atlas.glyph(font->content_hash, 2u, 20.0f), nullptr);
    EXPECT_EQ(atlas.count(), 1u);
    // act the atlas is cleared, which a test does so one test cannot answer another (§2.2)
    atlas.clear();
    // assert the bitmaps are gone, so a later request rasterizes again (§6.3)
    EXPECT_EQ(atlas.count(), 0u);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareTextChecks, TheAtlasTextKeepsTheFillColour) {
    // arrange the carried fixture and an embedded run, which the atlas draws (§6.3, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 40.0f);
    ASSERT_NE(layout.run, nullptr);
    const std::unique_ptr<software::SoftwareRenderSurface> surface = software::SoftwareRenderSurface::create(WindowMetrics::from_logical(Size{80.0f, 60.0f}, 1.0f));
    ASSERT_NE(surface, nullptr);
    // act the run is drawn in red, which is a fill whose channels differ (§6.3)
    RenderContext& context = surface->begin_frame();
    context.draw_text(layout, Point{8.0f, 8.0f}, Paint{.fill = Color::rgba(1.0f, 0.0f, 0.0f, 1.0f)});
    surface->end_frame();
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    // assert the ink is red rather than blue, which is what a swapped channel would produce (§6.3)
    bool red_ink = false;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        red_ink = red_ink || (pixels[index] > 200u && pixels[index + 2] < 50u);
    EXPECT_TRUE(red_ink);
    // exit
    resources::FontRegistry::clear();
}

TEST(SoftwareTextChecks, TheSoftwareContextForwardsEveryDrawToTheSharedContext) {
    // arrange a software surface, which the reference draws through (§6.3)
    const std::unique_ptr<software::SoftwareRenderSurface> surface = software::SoftwareRenderSurface::create(WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f));
    ASSERT_NE(surface, nullptr);
    RenderContext& context = surface->begin_frame();
    // act every non text draw is forwarded, which keeps the two backends one interpretation (§6.3)
    context.save();
    context.set_transform(Transform{.translate_x = 1.0f, .translate_y = 2.0f});
    EXPECT_FLOAT_EQ(context.transform().translate_x, 1.0f);
    context.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{32.0f, 32.0f}));
    context.clip_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}), 4.0f);
    EXPECT_FALSE(context.clip().empty());
    const std::vector<Point> points{Point{0.0f, 0.0f}, Point{10.0f, 10.0f}};
    const std::shared_ptr<const LineBatch> batch = std::make_shared<const LineBatch>(1u, std::span<const Point>(points));
    context.draw_line_batch(batch, Transform{}, StrokeStyle{});
    std::vector<DrawCommand> commands;
    commands.push_back(DrawCommand::rectangle(Rect::from_origin_size(Point{2.0f, 2.0f}, Size{4.0f, 4.0f}), Paint{.fill = Color::rgb(0.0f, 0.0f, 0.0f)}));
    const std::shared_ptr<const PictureResource> picture = std::make_shared<const PictureResource>(2u, std::move(commands));
    context.draw_picture(picture, Transform{});
    context.restore();
    // an empty run is drawn as nothing, which is the context's quiet default (§6.3)
    context.draw_text(TextLayout{}, Point{0.0f, 0.0f}, Paint{});
    surface->end_frame();
    // assert the frame finished, which is what the forwarded draws produce (§6.3)
    EXPECT_FALSE(surface->read_pixels().empty());
}

TEST(VendoredCacheChecks, ACacheHitSkipsShapingAndClearDropsIt) {
    // arrange an engine that can shape, which the cache test needs (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    ASSERT_NE(engine, nullptr);
    // act the same run is measured twice, which is the steady state of a label (§9.1)
    (void)engine->measure("Albor", 500.0f, 16.0f);
    (void)engine->measure("Albor", 500.0f, 16.0f);
    // assert one shaping pass happened and one entry is held (§9.1)
    EXPECT_EQ(engine->shaped_run_count(), 1u);
    EXPECT_EQ(engine->cache_size(), 1u);
    // act the cache is dropped, which a memory pressure policy does (§9.1)
    engine->clear_cache();
    // assert the drop leaves no entry behind (§9.1)
    EXPECT_EQ(engine->cache_size(), 0u);
    // act the same run is measured after the drop, which must shape again (§9.1)
    (void)engine->measure("Albor", 500.0f, 16.0f);
    // assert the later measure is a new shaping pass (§9.1)
    EXPECT_EQ(engine->shaped_run_count(), 2u);
}

TEST(TextEngineInstallChecks, ABuildWithoutACarriedFontFallsBackToThePlatformFace) {
    // arrange no carried font and no named family, which is an example without an embedded font (§9.1, AD-28)
    resources::FontRegistry::clear();
    set_installed_font_family("");
    // act installing selects the vendored engine, and the factory falls back to the platform face (§9.1, AD-28)
    harfbuzz::install_harfbuzz_text_engine();
    const std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert the platform face keeps text available, which is the transitional path AD-34 replaces (§9.1, AD-28)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    // exit
    set_text_engine_factory(nullptr);
    set_installed_font_family("");
    resources::FontRegistry::clear();
}

TEST(HarfbuzzFontFaceChecks, CarriesTheBytesItWasBuiltFrom) {
    // arrange the font the build carries, read whole because a face is built from a complete file (§15, AD-34)
    std::ifstream file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    ASSERT_TRUE(file.good());
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // act
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> face = harfbuzz::HarfbuzzFontFace::create(bytes);
    // assert the face keeps the bytes it was built from and identifies them by content (§15, AD-34)
    ASSERT_NE(face, nullptr);
    EXPECT_NE(face->hash(), 0u);
    EXPECT_EQ(face->bytes().size(), bytes->size());
    EXPECT_EQ(face->bytes().front(), bytes->front());
}

TEST(HarfbuzzFontFaceChecks, RefusesBytesThatCarryNoFont) {
    // arrange the three buffers a build could hand a face (§15, AD-34)
    const std::shared_ptr<const std::vector<std::uint8_t>> nothing;
    const auto empty = std::make_shared<const std::vector<std::uint8_t>>();
    const auto noise = std::make_shared<const std::vector<std::uint8_t>>(64u, 0x5au);
    // act
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> missing = harfbuzz::HarfbuzzFontFace::create(nothing);
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> blank = harfbuzz::HarfbuzzFontFace::create(empty);
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> garbage = harfbuzz::HarfbuzzFontFace::create(noise);
    // assert none of them carries a face, which the factory refuses rather than half builds (§9.1)
    EXPECT_EQ(missing, nullptr);
    EXPECT_EQ(blank, nullptr);
    EXPECT_EQ(garbage, nullptr);
}

TEST(HarfbuzzFontFaceChecks, OutlinesAGlyphFromItsOwnBytes) {
    // arrange the font the build carries, and a glyph the engine shaped from that same file (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    ASSERT_FALSE(layout.run->lines().front().glyphs.empty());
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    std::ifstream file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    ASSERT_TRUE(file.good());
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> face = harfbuzz::HarfbuzzFontFace::create(bytes);
    ASSERT_NE(face, nullptr);
    // act
    const GlyphOutlineData outline = face->outline(glyph, 24.0f);
    // assert the face reports the closed contours the renderer fills, which no engine was needed for (§6.4, §9.1)
    ASSERT_FALSE(outline.points.empty());
    EXPECT_EQ(outline.verbs.front(), PathVerb::MoveTo);
    EXPECT_EQ(outline.verbs.back(), PathVerb::Close);
    // assert a size of no pixels and a glyph the face leaves empty both draw nothing (§9.1, AD-27)
    EXPECT_TRUE(face->outline(glyph, 0.0f).points.empty());
    EXPECT_TRUE(face->outline(0xFFFFu, 24.0f).points.empty());
    // exit
    resources::FontRegistry::clear();
}

TEST(HarfbuzzFontFaceChecks, OutlinesAGlyphForAHandleWithNoPlatformFace) {
    // arrange a face built from the carried bytes, which is the whole of what a backend without a platform face has (§9.1)
    std::ifstream file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    ASSERT_TRUE(file.good());
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::shared_ptr<const harfbuzz::HarfbuzzFontFace> face = harfbuzz::HarfbuzzFontFace::create(bytes);
    ASSERT_NE(face, nullptr);
    // the glyph of "A" is shaped over the same bytes, which is the glyph a run would carry (§9.1, AD-34)
    resources::FontRegistry::clear();
    resources::FontRegistry::register_font(make_embedded_font(ALBOR_TEST_FONT_FAMILY, ALBOR_TEST_FONT_PATH));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    ASSERT_NE(engine, nullptr);
    const TextLayout layout = engine->measure("A", 0.0f, 24.0f);
    ASSERT_FALSE(layout.empty());
    const std::uint32_t glyph = layout.run->lines().front().glyphs.front().id;
    // the registry is emptied, so only the handle's own face can answer the walk (§15, AD-34)
    resources::FontRegistry::clear();
    // act the handle carries the face and no platform typeface, which is the windows and linux case (§9.1, AD-34)
    const FontHandle handle = FontHandle::from_face(face);
    const render::GlyphPath path = render::glyph_path(handle, glyph, 24.0f);
    // assert the portable walk returns the closed contour a fill needs, with no platform face behind it (§6.4)
    ASSERT_FALSE(path.points.empty());
    EXPECT_EQ(path.verbs.front(), PathVerb::MoveTo);
    EXPECT_EQ(path.verbs.back(), PathVerb::Close);
    // exit
    resources::FontRegistry::clear();
}
