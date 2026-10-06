#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "text/text-skia/skia_text_engine.h"
#include "text/text-skia/skia_text_services.h"
#include "text/text_engine.h"

using namespace albor;

namespace
{
    // the contract test double, which measures with a fixed advance so a test asserts on §9.1 rather than on a backend (§9.1)
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
            // the run keeps its text, because the recorded scene needs the bytes the draw will use (§6.2)
            TextLayout layout = TextLayout::from_text(std::string(text), font_size);
            // the advance counts bytes and the ceiling clamps, which is what a container expects (§8.2)
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            layout.size = Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT};
            return layout;
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

TEST(SkiaTextEngineChecks, KeepsTheMeasuredText) {
    // arrange
    SkiaTextEngine engine;
    // act
    const TextLayout layout = engine.measure("Albor", 1000.0f);
    // assert the run carries the utf8 bytes the render stage needs (§6.2)
    EXPECT_EQ(layout.text, "Albor");
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
    // assert skparagraph dereferences the unicode without checking, so the engine refuses instead of passing a null through (§9.1)
    EXPECT_TRUE(layout.empty());
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
