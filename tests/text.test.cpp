#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "render/shaped_run.h"
#include "text/text-skia/skia_text_engine.h"
#include "text/text-skia/skia_text_services.h"
#include "text/text_engine.h"

using namespace albor;

namespace
{
    // the contract test double, which measures with a fixed advance so a test asserts on §9.1 (§9.1)
    class FixedAdvanceEngine final : public TextEngine
    {
    public:
        // the advance one byte contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height any run reports, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // whether the double reports itself as able to measure (§9.1)
        bool usable = true;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // an engine that cannot measure reports nothing, which is what an absent backend looks like (§9.1)
            if (!usable)
                return TextLayout{};
            // the double shapes one glyph per character, which stands in for shaping without a font backend (§9.1)
            ShapedRun::Line line;
            // the line origin is the baseline the synthesized glyphs hang from (§9.1)
            line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            // every character becomes one glyph placed a fixed advance apart, which is what the double measures (§9.1)
            for (const char character : text)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}});
            // the shaped run travels beside the extents the layout reports, which is what a measurement hands over (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(line)}, font_size), .size = Size{.width = std::min(static_cast<float>(text.size()) * ADVANCE, max_width > 0.0f ? max_width : static_cast<float>(text.size()) * ADVANCE), .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        [[nodiscard]] bool ready() const override {
            // the double reports the flag a test sets, so an unusable engine can be exercised (§9.1)
            return usable;
        }
    };
} // namespace

TEST(TextEngineContractChecks, StartsWithoutABackendInstalled) {
    // arrange / act
    const TextEngineFactory& factory = text_engine_factory();
    // assert the framework reports no backend rather than fabricating one (§9.1)
    EXPECT_FALSE(static_cast<bool>(factory));
}

TEST(TextEngineContractChecks, CreateRefusesWithoutABackend) {
    // arrange / act
    std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert an uninstalled backend means the framework cannot measure text at all (§9.1)
    EXPECT_EQ(engine, nullptr);
}

TEST(TextEngineContractChecks, InstalledBackendIsBuilt) {
    // arrange
    set_text_engine_factory([] { return std::make_unique<FixedAdvanceEngine>(); });
    // act
    std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert the installed factory is what the framework now measures through (§9.1)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    set_text_engine_factory(nullptr);
}

TEST(TextEngineContractChecks, InstalledBackendIsReplaceable) {
    // arrange
    set_text_engine_factory([] { return std::make_unique<FixedAdvanceEngine>(); });
    set_text_engine_factory([] { return std::make_unique<FixedAdvanceEngine>(); });
    // act / assert a second install replaces the first, because the framework measures through exactly one engine (§9.1)
    EXPECT_TRUE(static_cast<bool>(text_engine_factory()));
    set_text_engine_factory(nullptr);
}

TEST(TextEngineContractChecks, ABackendThatIsNotReadyIsReported) {
    // arrange
    FixedAdvanceEngine engine;
    engine.usable = false;
    // act / assert the contract reports the state, so a caller can refuse to size a scene (§9.1)
    EXPECT_FALSE(engine.ready());
}

TEST(TextEngineContractChecks, AnUnreadyBackendMeasuresAnEmptyRun) {
    // arrange
    FixedAdvanceEngine engine;
    engine.usable = false;
    // act
    const TextLayout layout = engine.measure("albor", 0.0f);
    // assert a backend with no font data has nothing to report, so it invents no extent (§9.1)
    EXPECT_TRUE(layout.empty());
}

TEST(TextEngineContractChecks, TheSharedConstantsAreUsable) {
    // arrange / act / assert the default size has to be positive, or a run measured without one would have no height (§9.1)
    EXPECT_GT(TextEngine::DEFAULT_FONT_SIZE, 0.0f);
    // the unbounded width has to exceed any real advance, or an unbounded run would wrap (§8.2)
    EXPECT_GT(TextEngine::UNBOUNDED_WIDTH, 1000.0f);
}

TEST(TextEngineContractChecks, ABackendHonoursTheCeiling) {
    // arrange
    FixedAdvanceEngine engine;
    // act
    const TextLayout layout = engine.measure("albor", 25.0f);
    // assert the ceiling is a wrapping width rather than a stretch (§8.2, §9.1)
    EXPECT_LE(layout.size.width, 25.0f);
    EXPECT_GT(layout.size.width, 0.0f);
}

TEST(TextEngineContractChecks, ABackendTreatsANonPositiveCeilingAsUnbounded) {
    // arrange
    FixedAdvanceEngine engine;
    // act
    const TextLayout zero_ceiling = engine.measure("albor", 0.0f);
    const TextLayout negative_ceiling = engine.measure("albor", -5.0f);
    // assert a container that does not constrain asks for the whole run (§8.2, §9.1)
    EXPECT_FLOAT_EQ(negative_ceiling.size.width, zero_ceiling.size.width);
}

TEST(SkiaTextEngineChecks, InitialisesReady) {
    // arrange / act
    SkiaTextEngine engine;
    // assert the engine resolves the system font collection and the icu unicode in its constructor (§9.1)
    EXPECT_TRUE(engine.ready());
}

TEST(SkiaTextEngineChecks, MeasuresAnEmptyRunAsNothing) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("", 100.0f);
    // assert a run with no glyphs occupies no height at all (§9.1)
    EXPECT_TRUE(layout.empty());
    EXPECT_FLOAT_EQ(layout.size.width, 0.0f);
    EXPECT_FLOAT_EQ(layout.size.height, 0.0f);
}

TEST(SkiaTextEngineChecks, MeasuresANonEmptyRunToAShapedExtent) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert shaping resolved real typefaces, so both axes carry a real extent (§9.1)
    EXPECT_FALSE(layout.empty());
    EXPECT_GT(layout.size.width, 0.0f);
    EXPECT_GT(layout.size.height, 0.0f);
}

TEST(SkiaTextEngineChecks, ShapesTheMeasuredTextIntoGlyphs) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert the render stage is handed glyphs rather than bytes (§9.1, AD-18)
    ASSERT_NE(layout.run, nullptr);
    EXPECT_EQ(layout.run->glyph_count(), 5u);
}

TEST(SkiaTextEngineChecks, EveryShapedLineCarriesTheTypefaceItWasShapedWith) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert a line names the typeface its glyphs belong to, so a renderer draws them in the right font (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 1u);
    EXPECT_TRUE(layout.run->lines()[0].font.valid());
}

TEST(SkiaTextEngineChecks, TheShapedRunCarriesTheRequestedFontSize) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 24.0f);
    // assert the font is built from the requested size, not from the run height (§9.1)
    EXPECT_FLOAT_EQ(layout.run->font_size(), 24.0f);
}

TEST(SkiaTextEngineChecks, TheGlyphsAdvanceAcrossTheLine) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert the shaped positions move rightwards, because shaping placed the glyphs in reading order (§9.1)
    const std::vector<ShapedGlyph>& glyphs = layout.run->lines()[0].glyphs;
    ASSERT_GE(glyphs.size(), 2u);
    EXPECT_LT(glyphs.front().position.x, glyphs.back().position.x);
}

TEST(SkiaTextEngineChecks, AWrappedRunShapesIntoSeveralBaselines) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 60.0f);
    const TextLayout unwrapped = engine.measure("the quick brown fox jumps over the lazy dog", 0.0f);
    // assert a wrapped run places every line on its own baseline, which is what stage two replays (§9.1)
    EXPECT_GT(wrapped.run->lines().size(), unwrapped.run->lines().size());
    EXPECT_GT(wrapped.run->lines().back().origin.y, wrapped.run->lines().front().origin.y);
}

TEST(SkiaTextEngineChecks, AWrappedRunShapesEveryGlyphButTheSpacesItBreaksOn) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout unwrapped = engine.measure("the quick brown fox jumps over the lazy dog", 0.0f);
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 60.0f);
    // assert a space at a line break shapes no glyph, so a break costs one glyph (§9.1)
    const std::size_t breaks = wrapped.run->lines().size() - 1;
    EXPECT_EQ(wrapped.run->glyph_count() + breaks, unwrapped.run->glyph_count());
}

TEST(SkiaTextEngineChecks, ASecondMeasureOfTheSameRunIsServedFromTheCache) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout first = engine.measure("cached", 200.0f);
    const std::size_t shaped_after_first = engine.shaped_run_count();
    // act
    const TextLayout second = engine.measure("cached", 200.0f);
    // assert a repeated measure shapes nothing and hands back the same run (§9.1)
    EXPECT_EQ(second.run, first.run);
    EXPECT_EQ(engine.shaped_run_count(), shaped_after_first);
    EXPECT_EQ(engine.cache_size(), 1u);
}

TEST(SkiaTextEngineChecks, ADifferentCeilingShapesAnotherRun) {
    // arrange
    SkiaTextEngine engine;
    (void)(engine.measure("twice shaped", 200.0f));
    const std::size_t shaped_before = engine.shaped_run_count();
    // act
    (void)(engine.measure("twice shaped", 80.0f));
    // assert the same text wrapped at another ceiling is another run, so the cache cannot serve the first (§9.1)
    EXPECT_EQ(engine.shaped_run_count(), shaped_before + 1);
}

TEST(SkiaTextEngineChecks, AnotherFontSizeShapesAnotherRun) {
    // arrange
    SkiaTextEngine engine;
    (void)(engine.measure("twice shaped", 200.0f, 16.0f));
    const std::size_t shaped_before = engine.shaped_run_count();
    // act
    (void)(engine.measure("twice shaped", 200.0f, 32.0f));
    // assert the same text shaped at another size is another run, so the cache cannot serve the first (§9.1)
    EXPECT_EQ(engine.shaped_run_count(), shaped_before + 1);
}

TEST(SkiaTextEngineChecks, DistinctRunsEachTakeAnEntry) {
    // arrange
    SkiaTextEngine engine;
    // act
    (void)(engine.measure("first", 0.0f));
    (void)(engine.measure("second", 0.0f));
    // assert the cache holds every distinct run, because a tree lays out more than one label (§9.1)
    EXPECT_EQ(engine.cache_size(), 2u);
}

TEST(SkiaTextEngineChecks, ClearingTheCacheDropsEveryRun) {
    // arrange
    SkiaTextEngine engine;
    (void)(engine.measure("first", 0.0f));
    (void)(engine.measure("second", 0.0f));
    // act
    engine.clear_cache();
    // assert a memory pressure policy empties the cache, because the ui thread can rebuild every run (§9.1)
    EXPECT_EQ(engine.cache_size(), 0u);
}

TEST(SkiaTextEngineChecks, TheCacheStaysBoundedWhenNothingIsEverRepeated) {
    // arrange an application that measures a different run every time would grow the cache without limit (§9.1)
    SkiaTextEngine engine;
    // act
    for (std::size_t index = 0; index < 600; index++)
        (void)(engine.measure("run " + std::to_string(index), 0.0f));
    // assert the cache drops its runs once it is full, so a runaway application cannot grow it without bound (§9.1)
    EXPECT_LE(engine.cache_size(), 512u);
    EXPECT_EQ(engine.shaped_run_count(), 600u);
}

TEST(SkiaTextEngineChecks, AMeasureAfterClearingShapesAgain) {
    // arrange
    SkiaTextEngine engine;
    const TextLayout first = engine.measure("rebuilt", 0.0f);
    engine.clear_cache();
    // act
    const TextLayout rebuilt = engine.measure("rebuilt", 0.0f);
    // assert the same inputs shape the same run again, so dropping the cache costs only the next shaping (§9.1)
    EXPECT_NE(rebuilt.run, first.run);
    EXPECT_EQ(*rebuilt.run, *first.run);
}

TEST(SkiaTextEngineChecks, AnEmptyRunIsNeverCached) {
    // arrange
    SkiaTextEngine engine;
    // act
    (void)(engine.measure("", 100.0f));
    // assert a run with nothing to shape costs no cache entry (§9.1)
    EXPECT_EQ(engine.cache_size(), 0u);
    EXPECT_EQ(engine.shaped_run_count(), 0u);
}

TEST(SkiaTextEngineChecks, WidthGrowsWithTheRun) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout short_run = engine.measure("abc", 10000.0f);
    const TextLayout long_run = engine.measure("abcdefghij", 10000.0f);
    // assert a longer run measures wider, because glyph advances accumulate (§9.1)
    EXPECT_GT(long_run.size.width, short_run.size.width);
}

TEST(SkiaTextEngineChecks, AShapedRunIsNotAFixedAdvance) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("abcd", 10000.0f);
    // assert shaping resolves real advances, so the result is not a byte count times a constant (§9.1)
    EXPECT_NE(layout.size.width, 4.0f * FixedAdvanceEngine::ADVANCE);
}

TEST(SkiaTextEngineChecks, HonoursTheCeiling) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("abcdefghijklmnop", 40.0f);
    // assert a run wider than the ceiling wraps rather than overflows (§8.2, §9.1)
    EXPECT_LE(layout.size.width, 40.0f);
    EXPECT_GT(layout.size.width, 0.0f);
}

TEST(SkiaTextEngineChecks, ACeilingTooSmallStillMeasures) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("abcdefghijklmnop", 25.0f);
    // assert the same ceiling holds when the run has to wrap more than once (§8.2, §9.1)
    EXPECT_LE(layout.size.width, 25.0f);
    EXPECT_GT(layout.size.width, 0.0f);
}

TEST(SkiaTextEngineChecks, AnUnboundedRunStaysOnOneLine) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("the quick brown fox jumps over the lazy dog", 0.0f);
    // assert a run with no ceiling does not wrap, so it is as wide as the whole phrase (§8.2, §9.1)
    EXPECT_GT(layout.size.width, 60.0f);
    EXPECT_GT(layout.size.height, 0.0f);
}

TEST(SkiaTextEngineChecks, AWrappingCeilingProducesMoreLines) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 60.0f);
    const TextLayout unwrapped = engine.measure("the quick brown fox jumps over the lazy dog", 0.0f);
    // assert wrapped lines stack, so the whole run is taller than a single line of it (§9.1)
    EXPECT_GT(wrapped.size.height, unwrapped.size.height);
}

TEST(SkiaTextEngineChecks, ReportsTheLineMetrics) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 0.0f);
    // assert the ascent and descent come from the shaped line, so a baseline draw can place it (§9.1)
    EXPECT_GT(layout.ascent, 0.0f);
    EXPECT_GT(layout.descent, 0.0f);
}

TEST(SkiaTextEngineChecks, AMetricsPairSumsToTheRunHeight) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 0.0f);
    // assert an unwrapped run is one line, so ascent plus descent is its whole height (§9.1)
    EXPECT_NEAR(layout.ascent + layout.descent, layout.size.height, 0.5f);
}

TEST(SkiaTextEngineChecks, AFontSizeScalesTheRunHeight) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout small = engine.measure("Albor", 0.0f, 8.0f);
    const TextLayout large = engine.measure("Albor", 0.0f, 32.0f);
    // assert metrics follow the requested size, which is what makes a container adapt (§9.1)
    EXPECT_GT(large.size.height, small.size.height);
}

TEST(SkiaTextEngineChecks, TheDefaultOverloadMatchesTheExplicitOne) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout defaulted = engine.measure("Albor", 0.0f);
    const TextLayout explicit_size = engine.measure("Albor", 0.0f, TextEngine::DEFAULT_FONT_SIZE);
    // assert the overload without a size has to agree with the one that names it (§9.1)
    EXPECT_FLOAT_EQ(defaulted.size.height, explicit_size.size.height);
}

TEST(SkiaTextEngineChecks, RepeatedMeasuresAreStable) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout first = engine.measure("repeat", 500.0f);
    const TextLayout second = engine.measure("repeat", 500.0f);
    // assert shaping the same run twice has to give the same answer, or layout would jitter (§7.3)
    EXPECT_EQ(first, second);
}

TEST(SkiaTextEngineChecks, MultiByteTextMeasuresToAShapedExtent) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("áé", 1000.0f);
    // assert shaping counts glyphs rather than utf8 bytes, so the result is not a multiple of the byte advance (§9.1)
    EXPECT_GT(layout.size.width, 0.0f);
}

TEST(SkiaTextEngineChecks, InstallMakesTheSkiaEngineTheFrameworkBackend) {
    // arrange / act
    SkiaTextEngine::install();
    std::unique_ptr<TextEngine> engine = create_text_engine();
    // assert installing is what lets the framework measure through this backend rather than a double (§9.1)
    ASSERT_NE(engine, nullptr);
    EXPECT_TRUE(engine->ready());
    set_text_engine_factory(nullptr);
}

TEST(SkiaTextEngineChecks, ThePlatformResolvesBothServices) {
    // arrange / act
    const TextServices services = platform_text_services();
    // assert the platform hands over a font collection and an icu unicode, which is what an engine shapes with (§9.1)
    EXPECT_TRUE(static_cast<bool>(services.font_manager));
    EXPECT_TRUE(static_cast<bool>(services.unicode));
}

TEST(SkiaTextEngineChecks, AnEngineWithoutAUnicodeIsNotReady) {
    // arrange the services carry a font collection but no unicode, which is what a platform failure looks like (§9.1)
    TextServices services = platform_text_services();
    services.unicode = nullptr;
    // act
    SkiaTextEngine engine(services);
    // assert without a unicode there is no line breaking, so the engine refuses to shape (§9.1)
    EXPECT_FALSE(engine.ready());
}

TEST(SkiaTextEngineChecks, AnEngineWithoutAUnicodeMeasuresNothing) {
    // arrange
    TextServices services = platform_text_services();
    services.unicode = nullptr;
    SkiaTextEngine engine(services);
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert skparagraph dereferences the unicode unchecked, so the engine refuses (§9.1)
    EXPECT_TRUE(layout.empty());
    EXPECT_EQ(layout.run, nullptr);
    EXPECT_FLOAT_EQ(layout.size.width, 0.0f);
}

TEST(SkiaTextEngineChecks, AnEngineWithNoServicesIsNotReady) {
    // arrange nothing resolved at all, which is the state a platform that has no font support leaves behind (§9.1)
    TextServices services;
    // act
    SkiaTextEngine engine(services);
    // assert
    EXPECT_FALSE(engine.ready());
}

TEST(SkiaTextEngineChecks, AnEngineWithNoServicesMeasuresNothing) {
    // arrange
    TextServices services;
    SkiaTextEngine engine(services);
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 24.0f);
    // assert an unready engine reports nothing rather than a fabricated extent (§9.1)
    EXPECT_TRUE(layout.empty());
    EXPECT_EQ(layout.run, nullptr);
    EXPECT_FLOAT_EQ(layout.ascent, 0.0f);
    EXPECT_FLOAT_EQ(layout.descent, 0.0f);
}

TEST(SkiaTextEngineChecks, AnEngineWithoutAFontManagerStillShapes) {
    // arrange the unicode is what skparagraph dereferences, so a missing font manager alone does not stop it (§9.1)
    TextServices services = platform_text_services();
    services.font_manager = nullptr;
    // act
    SkiaTextEngine engine(services);
    // assert
    EXPECT_TRUE(engine.ready());
}

TEST(SkiaTextEngineChecks, EveryShapedLineCarriesGlyphsRelativeToItsOwnBaseline) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    // assert every glyph sits on the baseline of its own line, which is what the draw path composes with the origin (§9.1)
    ASSERT_GT(wrapped.run->lines().size(), 1u);
    for (const ShapedRun::Line& line : wrapped.run->lines())
        for (const ShapedGlyph& glyph : line.glyphs)
            EXPECT_FLOAT_EQ(glyph.position.y, 0.0f);
}

TEST(SkiaTextEngineChecks, EveryShapedLineAdvancesItsGlyphsToTheRight) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    // assert the glyphs of a line advance along it, because shaping places them in reading order (§9.1)
    const std::vector<ShapedGlyph>& glyphs = layout.run->lines()[0].glyphs;
    ASSERT_GT(glyphs.size(), 1u);
    EXPECT_GT(glyphs[1].position.x, glyphs[0].position.x);
}

TEST(ShapedRunChecks, AGlyphCarriesTheByteItWasShapedFrom) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 1}};
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a glyph traces back to the byte it came from, which is what places a caret against it (§9.1)
    EXPECT_EQ(run->lines().front().glyphs[1].cluster, 1u);
}

TEST(ShapedRunChecks, ALineCarriesTheBytesItSpans) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 4}};
    line.start_byte = 4;
    line.end_byte = 5;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert the span is the byte range the line occupies, which is what resolves a caret to a row (§9.1)
    EXPECT_EQ(run->lines().front().start_byte, 4u);
    EXPECT_EQ(run->lines().front().end_byte, 5u);
}

TEST(ShapedRunChecks, ALineCarriesTheAdvanceACaretStopsAt) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.width = 42.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert the advance is where a caret past the last glyph rests (§9.1)
    EXPECT_FLOAT_EQ(run->lines().front().width, 42.0f);
}

TEST(ShapedRunChecks, ALineCarriesTheAscentAndDescentItWasMeasuredAt) {
    // arrange
    ShapedRun::Line line;
    line.ascent = 13.0f;
    line.descent = 4.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert the pair is per line, because a fallback or a tall run moves it (§9.1)
    EXPECT_FLOAT_EQ(run->lines().front().ascent, 13.0f);
    EXPECT_FLOAT_EQ(run->lines().front().descent, 4.0f);
}

TEST(ShapedRunChecks, AnEmptyRunResolvesEveryByteToItsFirstLine) {
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({}, 16.0f);
    // assert a run that shaped no line has no row to report, so the first index stands (§9.1)
    EXPECT_EQ(run->line_at_byte(0), 0u);
}

TEST(ShapedRunChecks, AByteResolvesToTheLineThatSpansIt) {
    // arrange
    ShapedRun::Line first;
    first.end_byte = 4;
    ShapedRun::Line second;
    second.start_byte = 4;
    second.end_byte = 9;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({first, second}, 16.0f);
    // assert a byte inside the second span resolves to the second line (§9.1)
    EXPECT_EQ(run->line_at_byte(4), 1u);
    EXPECT_EQ(run->line_at_byte(8), 1u);
}

TEST(ShapedRunChecks, AByteBeforeEveryLineResolvesToTheFirstOne) {
    // arrange
    ShapedRun::Line first;
    first.start_byte = 6;
    first.end_byte = 9;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({first}, 16.0f);
    // assert a byte before the run resolves to the first line rather than past the end (§9.1)
    EXPECT_EQ(run->line_at_byte(0), 0u);
}

TEST(ShapedRunChecks, ABytePastEveryLineResolvesToTheLastOne) {
    // arrange
    ShapedRun::Line first;
    first.end_byte = 2;
    ShapedRun::Line second;
    second.start_byte = 2;
    second.end_byte = 5;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({first, second}, 16.0f);
    // assert a byte past the run clamps to the last line, because a run reports no line it did not shape (§9.1)
    EXPECT_EQ(run->line_at_byte(99), 1u);
}

TEST(ShapedRunChecks, ACaretAtTheFirstByteSitsAtTheLineOrigin) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{5.0f, 0.0f}, .cluster = 0}};
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a caret before the first glyph sits where that glyph starts (§9.1)
    EXPECT_FLOAT_EQ(run->x_at_byte(0, 0), 5.0f);
}

TEST(ShapedRunChecks, ACaretAtAByteSitsWhereItsGlyphStarts) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 1}};
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a caret before the second glyph sits at the pen that placed it (§9.1)
    EXPECT_FLOAT_EQ(run->x_at_byte(0, 1), 10.0f);
}

TEST(ShapedRunChecks, ACaretPastTheLastGlyphSitsAtTheLineAdvance) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.start_byte = 0;
    line.end_byte = 1;
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a caret past every glyph rests at the end of the line's advance (§9.1)
    EXPECT_FLOAT_EQ(run->x_at_byte(0, 1), 20.0f);
}

TEST(ShapedRunChecks, ACaretOnALineWithNoGlyphsSitsAtTheOrigin) {
    // arrange
    ShapedRun::Line line;
    line.start_byte = 3;
    line.end_byte = 4;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a blank line occupies a real position, and the caret on it is at the origin (§9.1)
    EXPECT_FLOAT_EQ(run->x_at_byte(0, 3), 0.0f);
}

TEST(ShapedRunChecks, ACaretOnALineTheRunDoesNotCarrySitsAtTheOrigin) {
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({}, 16.0f);
    // assert a query against a line that was never shaped reports the origin rather than reading past the end (§9.1)
    EXPECT_FLOAT_EQ(run->x_at_byte(7, 0), 0.0f);
}

TEST(ShapedRunChecks, APositionLeftOfTheFirstGlyphResolvesToTheFirstByte) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{10.0f, 0.0f}, .cluster = 2}};
    line.start_byte = 2;
    line.end_byte = 3;
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a press before the line resolves to the byte the line begins at (§9.1)
    EXPECT_EQ(run->byte_at_x(0, 0.0f), 2u);
}

TEST(ShapedRunChecks, APositionBetweenGlyphsResolvesToTheByteItPassed) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 1}};
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a press past the first glyph and short of the second snaps back to the byte it passed (§9.1)
    EXPECT_EQ(run->byte_at_x(0, 15.0f), 1u);
}

TEST(ShapedRunChecks, APositionPastTheAdvanceResolvesToTheEndOfTheLine) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.start_byte = 0;
    line.end_byte = 1;
    line.width = 20.0f;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a press beyond the last glyph resolves to the end of the line (§9.1)
    EXPECT_EQ(run->byte_at_x(0, 500.0f), 1u);
}

TEST(ShapedRunChecks, APositionOnALineWithNoGlyphsResolvesToWhereTheLineBegins) {
    // arrange
    ShapedRun::Line line;
    line.start_byte = 3;
    line.end_byte = 4;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a blank line resolves any press to the byte it begins at (§9.1)
    EXPECT_EQ(run->byte_at_x(0, 99.0f), 3u);
}

TEST(ShapedRunChecks, APositionOnALineTheRunDoesNotCarryResolvesToTheStartOfTheText) {
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({}, 16.0f);
    // assert a query against a line that was never shaped reports the start rather than reading past the end (§9.1)
    EXPECT_EQ(run->byte_at_x(7, 0.0f), 0u);
}

TEST(ShapedRunChecks, SteppingForwardWalksOneClusterAtATime) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 1}, ShapedGlyph{.id = 9, .position = Point{20.0f, 0.0f}, .cluster = 2}};
    line.start_byte = 0;
    line.end_byte = 3;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a caret advances one character at a time, because a cluster carries one character (§9.1)
    EXPECT_EQ(run->next_boundary_byte(0, 0), 1u);
    EXPECT_EQ(run->next_boundary_byte(0, 1), 2u);
}

TEST(ShapedRunChecks, SteppingForwardOffTheLastClusterStopsAtTheLineEnd) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.start_byte = 0;
    line.end_byte = 1;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert the last step lands on the end of the line rather than past it (§9.1)
    EXPECT_EQ(run->next_boundary_byte(0, 0), 1u);
}

TEST(ShapedRunChecks, SteppingForwardFromTheEndOfALineStays) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.start_byte = 0;
    line.end_byte = 1;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a line with nothing further does not step past its own end (§9.1)
    EXPECT_EQ(run->next_boundary_byte(0, 1), 1u);
}

TEST(ShapedRunChecks, SteppingBackWalksOneClusterAtATime) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 1}, ShapedGlyph{.id = 9, .position = Point{20.0f, 0.0f}, .cluster = 2}};
    line.start_byte = 0;
    line.end_byte = 3;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a caret retreats one character at a time, because a cluster carries one character (§9.1)
    EXPECT_EQ(run->previous_boundary_byte(0, 2), 1u);
    EXPECT_EQ(run->previous_boundary_byte(0, 1), 0u);
}

TEST(ShapedRunChecks, SteppingBackOffTheFirstClusterStays) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}};
    line.start_byte = 0;
    line.end_byte = 1;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a line with nothing before it does not step past its own start (§9.1)
    EXPECT_EQ(run->previous_boundary_byte(0, 0), 0u);
}

TEST(ShapedRunChecks, AMergedClusterStepsOnceForEveryGlyphItCarries) {
    // arrange
    ShapedRun::Line line;
    line.glyphs = {ShapedGlyph{.id = 7, .position = Point{0.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 8, .position = Point{10.0f, 0.0f}, .cluster = 0}, ShapedGlyph{.id = 9, .position = Point{20.0f, 0.0f}, .cluster = 1}};
    line.start_byte = 0;
    line.end_byte = 2;
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({line}, 16.0f);
    // assert a ligature shaped from several characters steps once, because a cluster is one character (§9.1)
    EXPECT_EQ(run->next_boundary_byte(0, 0), 1u);
    EXPECT_EQ(run->previous_boundary_byte(0, 1), 0u);
}

TEST(ShapedRunChecks, StepsOnALineTheRunDoesNotCarryAreRefusedQuietly) {
    // act
    const std::shared_ptr<const ShapedRun> run = ShapedRun::make({}, 16.0f);
    // assert stepping against a line that was never shaped leaves the offset alone (§9.1)
    EXPECT_EQ(run->next_boundary_byte(7, 4), 4u);
    EXPECT_EQ(run->previous_boundary_byte(7, 4), 4u);
}

TEST(SkiaTextEngineChecks, EveryShapedGlyphCarriesTheByteItCameFrom) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    // assert the first glyph traces to the first byte, which is what maps a caret onto the run (§9.1)
    const std::vector<ShapedGlyph>& glyphs = layout.run->lines()[0].glyphs;
    ASSERT_FALSE(glyphs.empty());
    EXPECT_EQ(glyphs.front().cluster, 0u);
}

TEST(SkiaTextEngineChecks, TheShapedClustersRiseWithTheText) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    // assert every glyph after the first carries a byte past the one before it, because shaping is in reading order (§9.1)
    const std::vector<ShapedGlyph>& glyphs = layout.run->lines()[0].glyphs;
    ASSERT_GT(glyphs.size(), 2u);
    EXPECT_LT(glyphs[0].cluster, glyphs[1].cluster);
    EXPECT_LT(glyphs[1].cluster, glyphs[2].cluster);
}

TEST(SkiaTextEngineChecks, AMeasuredRunSpansTheWholeText) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f, 20.0f);
    // assert the run begins at the first byte and ends at the last, because one line holds all of it (§9.1)
    EXPECT_EQ(layout.run->lines().front().start_byte, 0u);
    EXPECT_EQ(layout.run->lines().front().end_byte, 5u);
}

TEST(SkiaTextEngineChecks, AMultiByteRunSpansEveryByte) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("\xC3\xA9t\xC3\xA9", 1000.0f, 20.0f);
    // assert the run ends past both two byte characters, because a byte offset is what the run records (§9.1)
    EXPECT_EQ(layout.run->lines().front().end_byte, 5u);
}

TEST(SkiaTextEngineChecks, EveryShapedLineCarriesItsOwnAscentAndDescent) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    // assert every line reports the pair it was laid out at, because a caller places content line by line (§9.1)
    ASSERT_GT(wrapped.run->lines().size(), 1u);
    for (const ShapedRun::Line& line : wrapped.run->lines()) {
        // assert a line the engine laid out reports a positive ascent, because it occupies a real baseline (§9.1)
        EXPECT_GT(line.ascent, 0.0f);
        // assert the descent is a distance, so it is not negative (§9.1)
        EXPECT_GE(line.descent, 0.0f);
    }
}

TEST(SkiaTextEngineChecks, TheFirstLineOfAWrappedRunSpansTheBeginningOfTheText) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    // assert the run begins at the first byte, so a caret at zero resolves to the first row (§9.1)
    EXPECT_EQ(wrapped.run->lines().front().start_byte, 0u);
}

TEST(SkiaTextEngineChecks, TheByteSpansOfAWrappedRunRiseWithoutOverlapping) {
    // arrange
    SkiaTextEngine engine;
    const std::string text = "the quick brown fox jumps over the lazy dog";
    // act
    const TextLayout wrapped = engine.measure(text, 80.0f, 20.0f);
    // assert the spans rise through the text, because the lines are in reading order (§9.1)
    std::size_t previous_end = 0;
    for (const ShapedRun::Line& line : wrapped.run->lines()) {
        // assert a line begins at or after the previous one ended, because a caret resolves to exactly one line (§9.1)
        EXPECT_GE(line.start_byte, previous_end);
        // assert a line spans at least its own start, because a blank line advances no byte (§9.1)
        EXPECT_GE(line.end_byte, line.start_byte);
        // the next line is measured against where this one ended (§9.1)
        previous_end = line.end_byte;
    }
    // assert the last line ends at the end of the text, which is what makes the run span all of it (§9.1)
    EXPECT_EQ(previous_end, text.size());
}

TEST(SkiaTextEngineChecks, ASpaceBrokenAtALineBreakIsNoCaretStop) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout wrapped = engine.measure("the quick brown fox jumps over the lazy dog", 80.0f, 20.0f);
    // assert the byte the break dropped belongs to neither line, which is what keeps a caret off it (§9.1)
    ASSERT_GT(wrapped.run->lines().size(), 1u);
    EXPECT_GT(wrapped.run->lines()[1].start_byte, wrapped.run->lines()[0].end_byte);
}

TEST(SkiaTextEngineChecks, ABlankLineStaysInTheRun) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("a\n\nb", 1000.0f, 20.0f);
    // assert the middle row shaped no glyph, so the run reports an empty line for it (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 3u);
    EXPECT_TRUE(layout.run->lines()[1].glyphs.empty());
}

TEST(SkiaTextEngineChecks, ACaretRoundTripWalksEveryByteOfTheText) {
    // arrange
    SkiaTextEngine engine;
    const std::string text = "Albor";
    const TextLayout layout = engine.measure(text, 1000.0f, 20.0f);
    // assert every byte survives a caret placed at it and read back from its position (§9.1)
    for (std::size_t byte = 0; byte <= text.size(); byte++) {
        // the caret is placed on the line the byte resolves to (§9.1)
        const std::size_t line = layout.run->line_at_byte(byte);
        // the position the caret is drawn at is what a press at that position must resolve back to (§9.1)
        const float x = layout.run->x_at_byte(line, byte);
        EXPECT_EQ(layout.run->byte_at_x(line, x), byte);
    }
}

TEST(SkiaTextEngineChecks, AMultiByteCharacterIsOneCaretStep) {
    // arrange
    SkiaTextEngine engine;
    const std::string text = "\xC3\xA9\xC3\xA9";
    const TextLayout layout = engine.measure(text, 1000.0f, 20.0f);
    // assert stepping forward from the start lands past the first character, not inside it (§9.1)
    const std::size_t stepped = layout.run->next_boundary_byte(0, 0);
    // assert the step is the width of the two byte character, because a cluster is one character (§9.1)
    EXPECT_EQ(stepped, 2u);
}

TEST(SkiaTextEngineChecks, AFallbackFontSplitIsOneRowRatherThanThree) {
    // arrange text whose glyphs the default face does not hold, which is what a fallback run is (§9.1)
    SkiaTextEngine engine;
    // the corpus is latin around three ideographs, which a macOS default face shapes with a fallback (§9.1)
    const std::string text = "a\u65E5\u672C\u8A9Eb";
    // act
    const TextLayout layout = engine.measure(text, 1000.0f, 20.0f);
    // assert the run carries one entry per visual row, because a fallback split one row into several runs (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 1u);
    // assert every byte of the text is shaped on that row, which is what one row means (§9.1)
    EXPECT_EQ(layout.run->lines().front().start_byte, 0u);
    EXPECT_EQ(layout.run->lines().front().end_byte, text.size());
    // assert the row holds one cluster per character, which is what a caret steps over (§9.1)
    EXPECT_EQ(layout.run->lines().front().glyphs.size(), 5u);
}

TEST(SkiaTextEngineChecks, EveryByteOfAFallbackSplitRowResolvesToThatRow) {
    // arrange the same corpus measured at an unbounded width, so one row holds all of it (§9.1)
    SkiaTextEngine engine;
    const std::string text = "a\u65E5\u672C\u8A9Eb";
    const TextLayout layout = engine.measure(text, 1000.0f, 20.0f);
    // act assert every byte resolves to the only row, because a fallback run is not a row (§9.1)
    for (std::size_t byte = 0; byte <= text.size(); byte++)
        EXPECT_EQ(layout.run->line_at_byte(byte), 0u);
}

TEST(SkiaTextEngineChecks, ACaretStepsBackOverAFallbackRunBoundary) {
    // arrange a caret standing where the fallback run begins, which is not a row boundary (§9.1)
    SkiaTextEngine engine;
    const std::string text = "a\u65E5\u672C\u8A9Eb";
    const TextLayout layout = engine.measure(text, 1000.0f, 20.0f);
    // act the step back from the byte after the first cluster
    const std::size_t back = layout.run->previous_boundary_byte(0, 1);
    // assert the step lands on the cluster before it, because a split row holds no row boundary (§9.1)
    EXPECT_EQ(back, 0u);
}

TEST(SkiaTextEngineChecks, ABlankRowOwnsTheByteItBeginsAt) {
    // arrange text whose middle row shaped no glyph, which is what a blank line is (§9.1)
    SkiaTextEngine engine;
    const TextLayout layout = engine.measure("a\n\nb", 1000.0f, 20.0f);
    // act assert the blank row is a row of its own with a byte range that is not inverted (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 3u);
    EXPECT_EQ(layout.run->lines()[1].start_byte, 2u);
    EXPECT_EQ(layout.run->lines()[1].end_byte, 2u);
    // assert the byte the blank row begins at resolves to that row, because a caret stops on a blank row (§9.1)
    EXPECT_EQ(layout.run->line_at_byte(2), 1u);
    // assert a press on the blank row resolves to the byte it begins at (§9.1)
    EXPECT_EQ(layout.run->byte_at_x(1, 0.0f), 2u);
    // assert the rows still tile forward, which is what makes the run span the whole text (§9.1)
    EXPECT_EQ(layout.run->lines()[0].start_byte, 0u);
    EXPECT_EQ(layout.run->lines()[2].end_byte, 4u);
}

TEST(SkiaTextEngineChecks, ABlankRowKeepsTheAscentOfTheLineItOccupies) {
    // arrange a run with a blank row, which still occupies a real baseline (§9.1)
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("a\n\nb", 1000.0f, 20.0f);
    // assert the blank row carries the same ascent as the rows around it (§9.1)
    ASSERT_EQ(layout.run->lines().size(), 3u);
    EXPECT_GT(layout.run->lines()[1].ascent, 0.0f);
    EXPECT_FLOAT_EQ(layout.run->lines()[1].ascent, layout.run->lines()[0].ascent);
}

TEST(SkiaTextEngineChecks, AWrappedRowIsStillOneRowAfterTheRewrite) {
    // arrange a run that wraps, which is the ordinary case the rewrite must not disturb (§9.1)
    SkiaTextEngine engine;
    const std::string text = "the quick brown fox jumps over the lazy dog";
    // act
    const TextLayout layout = engine.measure(text, 80.0f, 20.0f);
    // assert more than one row came back, because the ceiling wrapped the text (§8.2)
    ASSERT_GT(layout.run->lines().size(), 1u);
    // assert every row carries the ascent of its own line, which is what a caret hangs from (§9.1)
    for (const ShapedRun::Line& line : layout.run->lines())
        EXPECT_GT(line.ascent, 0.0f);
    // assert the rows rise through the text and end at its end (§9.1)
    std::size_t previous_end = 0;
    for (const ShapedRun::Line& line : layout.run->lines()) {
        EXPECT_GE(line.start_byte, previous_end);
        EXPECT_GE(line.end_byte, line.start_byte);
        previous_end = line.end_byte;
    }
    EXPECT_EQ(previous_end, text.size());
}
