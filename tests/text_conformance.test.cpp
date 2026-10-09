#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "render/shaped_run.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text-skia/skia_text_engine.h"

using namespace albor;

namespace
{
    // the font both engines shape with, so the comparison is of shapers and not of fonts (§15, §9.1)
    const char* const FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf";

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

    // the largest distance between two glyph positions, which is the measure the tolerance is written against (§15)
    float position_distance(Point left, Point right) {
        // the axes differ independently, so the larger one is the distance (§15)
        return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
    }
} // namespace

TEST(DualEngineConformanceChecks, TheVendoredEngineLoadsAFont) {
    // arrange, act
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
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
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create("/no/such/font.ttf");
    // assert an engine with no face is refused, because a half built one would shape nothing silently (§9.1)
    EXPECT_EQ(engine, nullptr);
}

TEST(DualEngineConformanceChecks, AnEmptyStringMeasuresNothing) {
    // arrange
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
    ASSERT_NE(vendored, nullptr);
    // act an empty string carries no glyph, which is what an empty layout means (§6.2)
    const TextLayout measured = vendored->measure("", 0.0f, 16.0f);
    // assert
    EXPECT_EQ(measured.run, nullptr);
}

TEST(DualEngineConformanceChecks, TheVendoredRunIsNotYetDrawable) {
    // arrange
    if (!has_font())
        GTEST_SKIP() << "no conformance font on this host";
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> vendored = harfbuzz::HarfbuzzTextEngine::create(FONT_PATH);
    ASSERT_NE(vendored, nullptr);
    const TextLayout measured = vendored->measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(measured.run, nullptr);
    // act assert the boundary is stated rather than implied, so a caller checks drawable() first (§6.2)
    EXPECT_FALSE(vendored->drawable());
    // the handle is empty for the same reason, and a frame drawn from it would carry no glyph (§6.2)
    EXPECT_FALSE(measured.run->lines().front().font.valid());
}

TEST(DualEngineConformanceChecks, TheOracleRunIsDrawable) {
    // arrange the oracle, which issues a typeface and therefore draws (§9.1, AD-28)
    SkiaTextEngine oracle;
    const TextLayout reference = oracle.measure("Albor", 0.0f, 16.0f);
    ASSERT_NE(reference.run, nullptr);
    // act assert the oracle is the drawable one, which is why it is kept as the oracle (§9.1)
    EXPECT_TRUE(reference.run->lines().front().font.valid());
}
