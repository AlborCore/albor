#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "render/shaped_run.h"
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
