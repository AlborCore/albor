#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "core/clipboard.h"
#include "focus/focus_manager.h"
#include "render/frame_scene.h"
#include "ui/scene_builder.h"
#include "widgets/text_field.h"

using namespace albor;

namespace
{
    // the clipboard test double, which holds what a copy handed over so a field test asserts on §3.5 (§3.5)
    class HoldingClipboard final : public Clipboard
    {
    public:
        // what the double carries, which a copy replaces and a paste reads (§3.5)
        std::string carried;

        void set_text(std::string_view text) override {
            // the double owns its copy, because the caller's view does not outlive the call (§3.5)
            carried = std::string(text);
        }

        [[nodiscard]] std::string text() const override {
            // the content is what a paste reads (§3.5)
            return carried;
        }

        [[nodiscard]] bool has_text() const override {
            // a surface with content from none answers no (§3.5)
            return !carried.empty();
        }

        [[nodiscard]] bool ready() const override {
            // the double is always reachable (§3.5)
            return true;
        }
    };

    // the text engine test double, which measures with a fixed advance and real character clusters (§9.1)
    class EditingTextEngine final : public TextEngine
    {
    public:
        // the advance one byte of text contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height the double reports for any run, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // whether the double reports itself as able to measure (§9.1)
        bool usable = true;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // the unused ceiling is read, because the ceiling is part of the call (§8.2)
            (void)max_width;
            // an engine that cannot measure reports nothing, which is what an absent backend looks like (§9.1)
            if (!usable)
                return TextLayout{};
            // an empty run carries no glyphs, so the double measures it as nothing (§9.1)
            if (text.empty())
                return TextLayout{};
            // the lines the run is laid out into, which the ceiling and the breaks decide (§9.1)
            std::vector<ShapedRun::Line> lines;
            // the current line is built as the walk goes, because a break opens the next one (§9.1)
            lines.push_back(ShapedRun::Line{});
            // how many characters sit on the line so far, which is what the ceiling counts against (§9.1)
            float pen = 0.0f;
            // every character becomes one glyph placed a fixed advance apart, carrying the byte it starts at (§9.1)
            for (std::size_t index = 0; index < text.size();) {
                // the lead byte opens a character, which the double resolves as the spans utf8 uses (§9.1)
                std::size_t span = 1;
                // a two byte lead is the shape the test corpus needs (§9.1)
                if ((text[index] & 0xE0) == 0xC0)
                    span = 2;
                // a hard break shapes no glyph, which is what the real engines drop (§9.1)
                if (text[index] == '\n') {
                    // the line ends past the break, which is the byte a caret stops at (§9.1)
                    lines.back().end_byte = index;
                    // the next line begins past the break byte (§9.1)
                    lines.push_back(ShapedRun::Line{});
                    lines.back().start_byte = index + 1;
                    pen = 0.0f;
                    index += 1;
                    continue;
                }
                // a ceiling that no longer fits breaks the line, which is what wrapping is (§9.1)
                if (max_width > 0.0f && pen + ADVANCE > max_width) {
                    // the line ends where its last glyph fits (§9.1)
                    lines.back().end_byte = index;
                    // the next line begins at this character (§9.1)
                    lines.push_back(ShapedRun::Line{});
                    lines.back().start_byte = index;
                    pen = 0.0f;
                }
                // the glyph hangs where a fixed advance places it, and carries its own byte (§9.1)
                ShapedRun::Line& line = lines.back();
                // the baseline of the line is the one every run line hangs from (§9.1)
                if (line.glyphs.empty())
                    line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f + static_cast<float>(lines.size() - 1) * HEIGHT};
                // the glyph sits a fixed advance apart, and carries its own byte (§9.1)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(text[index]), .position = Point{.x = pen, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(index)});
                // the pen advances by one character, which is what the next glyph reads (§9.1)
                pen += ADVANCE;
                // the double steps the whole character, because a cluster is one character (§9.1)
                index += span;
            }
            // the last line ends at the whole text, which the walk left behind (§9.1)
            lines.back().end_byte = text.size();
            // every line carries its span, its baseline and its advance (§9.1)
            float width = 0.0f;
            // the extents are computed before the run moves the lines, because the move takes their glyphs (§9.1)
            for (std::size_t index = 0; index < lines.size(); index++) {
                // the line is addressed once, because its index carries the baseline every row hangs from (§9.1)
                ShapedRun::Line& line = lines[index];
                // a row that shaped no glyph still hangs from its own baseline, which a caret maps against (§9.1)
                if (line.glyphs.empty())
                    line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f + static_cast<float>(index) * HEIGHT};
                // the advance a caret stops at is what the line is (§9.1)
                line.width = static_cast<float>(line.glyphs.size()) * ADVANCE;
                // the pair is what every line box ties the paint to (§9.1)
                line.ascent = HEIGHT * 0.8f;
                line.descent = HEIGHT * 0.2f;
                // the longest line is what the run width reports (§9.1)
                width = std::max(width, line.width);
            }
            // the run height is what every line adds up to (§9.1)
            const float height = static_cast<float>(lines.size()) * HEIGHT;
            // the shaped run travels beside the extents the layout reports, which are the character extents (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(lines)}, font_size), .size = Size{.width = width, .height = height}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        [[nodiscard]] bool ready() const override {
            // the double reports the flag a test sets, so an unusable engine can be exercised (§9.1)
            return usable;
        }
    };

    // the rectangles of the shape commands of a scene, which is what a paint assertion reads off the one list (§6.1)
    std::vector<Rect> rect_commands(const FrameScene& scene) {
        // the shapes are gathered in paint order, because that is the paint order the list carries (§6.1)
        std::vector<Rect> rects;
        // every command is read, because a scene interleaves kinds (§6.1)
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == DrawCommandKind::Rect)
                rects.push_back(command.rect_geometry().rect);
        return rects;
    }

    // the commands of the runs of a scene, which is what a run assertion reads off the one list (§6.1)
    std::size_t text_command_count(const FrameScene& scene) {
        // the runs are counted, because a hint and a value draw the one kind (§6.1)
        std::size_t count = 0;
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == DrawCommandKind::Text)
                count++;
        return count;
    }

    // the proportional text engine double, where each character has its own advance (§9.1)
    class ProportionalTextEngine final : public TextEngine
    {
    public:
        // the advance one character takes, in logical pixels, which a fixed one cannot (§9.1)
        static constexpr float ADVANCE = 10.0f;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // the ceiling is read and ignored, because this double never wraps (§9.1)
            (void)max_width;
            // an empty run carries no line, which is what an engine reports for nothing (§9.1)
            if (text.empty())
                return TextLayout{};
            // the single line every run of this double occupies, which is what a per character advance builds (§9.1)
            ShapedRun::Line line;
            // the baseline the line hangs from, which is what the run draws at (§9.1)
            line.origin = Point{.x = 0.0f, .y = 13.0f};
            // the pen walks the characters, so the first glyph is narrow and the second is wide (§9.1)
            float pen = 0.0f;
            // the ascent and the descent the double reports for its one line (§9.1)
            const float ascent = 13.0f;
            const float descent = 4.0f;
            // every character becomes one glyph, which is the cluster the caret steps over (§9.1)
            for (std::size_t index = 0; index < text.size(); index++) {
                // the first character is twice as wide as the rest, so a prefix differs from the mean (§9.1)
                const float advance = index == 0 ? 2.0f * ADVANCE : ADVANCE;
                // the glyph sits where the pen left off and carries its own byte (§9.1)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(text[index]), .position = Point{.x = pen, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(index)});
                // the pen moves by that character's own advance (§9.1)
                pen += advance;
            }
            // the line spans the whole text, because the double wraps nothing (§9.1)
            line.start_byte = 0;
            // the line ends at the text end, which is what a caret past the last glyph resolves to (§9.1)
            line.end_byte = text.size();
            // the line is as wide as the pen walked, which is what a prefix of it is measured against (§9.1)
            line.width = pen;
            // the metrics sit on the line, because a caller places content by the pair (§9.1)
            line.ascent = ascent;
            // the descent is the pair member (§9.1)
            line.descent = descent;
            // the run holds that one line, which is the whole shaped result (§9.1)
            return TextLayout{.run = ShapedRun::make(std::vector<ShapedRun::Line>{line}, font_size), .size = Size{.width = pen, .height = ascent + descent}, .ascent = ascent, .descent = descent};
        }

        [[nodiscard]] bool ready() const override {
            // the double always measures, because its shapes are fixed (§9.1)
            return true;
        }
    };

} // namespace

TEST(TextFieldChecks, MeasuresToAtLeastTheMinimumExtent) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    Size measured{};
    // act
    field.measure(SizeConstraints::unconstrained(), measured);
    // assert a bare field is sized so it reads as an editable plate (§8.2)
    EXPECT_GE(measured.height, 20.0f);
}

TEST(TextFieldChecks, AValueWidensTheMeasuredExtent) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    Size empty{};
    field.measure(SizeConstraints::unconstrained(), empty);
    // act
    field.set_value("abcdefghijklmnopqrstuvwxyz");
    Size filled{};
    field.measure(SizeConstraints::unconstrained(), filled);
    // assert a wider run measures wider, because the plate grows around it (§8.2)
    EXPECT_GT(filled.width, empty.width);
}

TEST(TextFieldChecks, TheMinimumKeepsTheHeightAgainstAShortRun) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_value("a");
    // act
    Size measured{};
    field.measure(SizeConstraints::unconstrained(), measured);
    // assert the plate keeps the minimum height, because the run is the guest (§8.2)
    EXPECT_GE(measured.height, 20.0f);
}

TEST(TextFieldChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_value("twelve chars");
    // act
    Size measured{};
    field.measure(SizeConstraints::at_most(Size{40.0f, 8.0f}), measured);
    // assert the parent decision is final (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 8.0f);
}

TEST(TextFieldChecks, AFieldWithNoEngineMeasuresToTheMinimum) {
    // arrange
    TextField field(nullptr, nullptr);
    // act
    Size measured{};
    field.measure(SizeConstraints::unconstrained(), measured);
    // assert a field that cannot measure keeps the plate itself (§9.1)
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(TextFieldChecks, AnUnarrangedFieldDrawsNothing) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
    EXPECT_EQ(text_command_count(*recorder.scene()), 0u);
}

TEST(TextFieldChecks, DrawsTheValueRun) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the value is the one run a filled field draws (§6.1)
    EXPECT_EQ(text_command_count(*recorder.scene()), 1u);
}

TEST(TextFieldChecks, AnEmptyFieldWithAPlaceholderDrawsTheHint) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_placeholder("Type here");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the hint is the run an empty field draws (§8)
    EXPECT_EQ(text_command_count(*recorder.scene()), 1u);
}

TEST(TextFieldChecks, AnEmptyFieldWithoutAPlaceholderDrawsNoRun) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert
    EXPECT_EQ(text_command_count(*recorder.scene()), 0u);
}

TEST(TextFieldChecks, ThePlaceholderDisappearsAfterTheFirstEdit) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_placeholder("Type here");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    field.set_value("a");
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert a hint appears only behind an empty value, which is what cleared means (§8)
    EXPECT_EQ(text_command_count(*recorder.scene()), 1u);
    EXPECT_FALSE(field.value().empty());
}

TEST(TextFieldChecks, TheCaretDrawsOverAFocusedEditableField) {
    // arrange a field the focus has reached, which is what makes its caret mean something (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    field.set_focused(true);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the caret is the one filled bar the focused field records (§6.1)
    EXPECT_GE(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(TextFieldChecks, AnUnfocusedFieldDrawsNoCaret) {
    // arrange a field the focus never reached, which is most of them in a form (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert no bar draws, because a caret on every field is not a caret (§8.3)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 0u);
    EXPECT_FALSE(field.shows_caret());
}

TEST(TextFieldChecks, ADisabledFieldDrawsNoCaretEvenWhenItHoldsTheFocus) {
    // arrange a disabled field, which refuses input and therefore cannot be focused (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_focused(true);
    field.set_disabled(true);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the bar is gone, because a refused field is not being edited (§8.3)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 0u);
}

TEST(TextFieldChecks, LosingTheFocusHidesTheCaret) {
    // arrange a focused field whose focus is then taken elsewhere (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_focused(true);
    // act
    field.set_focused(false);
    // assert the field no longer claims to hold it, and its caret is hidden (§8.3)
    EXPECT_FALSE(field.focused());
    EXPECT_FALSE(field.shows_caret());
}

TEST(TextFieldChecks, AReadOnlyFieldDrawsNoCaret) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_caret(2);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert a plain writer has no bar to stand at, because it is not an editor (§8.3)
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(TextFieldChecks, SetCaretRedraws) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.clear_dirty();
    // act
    field.set_caret(3);
    // assert
    EXPECT_TRUE(field.render_dirty());
}

TEST(TextFieldChecks, AnUnchangedCaretRedrawsNothing) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    field.clear_dirty();
    // act
    field.set_caret(3);
    // assert
    EXPECT_FALSE(field.render_dirty());
}

TEST(TextFieldChecks, TheCaretClampsToTheValueLength) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    // act
    field.set_caret(999);
    // assert an offset past the value stands at its end, because there is no character there (§8.3)
    EXPECT_EQ(field.caret(), 5u);
}

TEST(TextFieldChecks, AHiddenFieldIsNotAHit) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_visible(false);
    // act
    const bool hit = field.hit_test(Point{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(TextFieldChecks, ADisabledFieldIsNotAHit) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_disabled(true);
    // act
    const bool hit = field.hit_test(Point{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(TextFieldChecks, HitsInsideItsBounds) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const bool hit = field.hit_test(Point{60.0f, 10.0f});
    // assert
    EXPECT_TRUE(hit);
}

TEST(TextFieldChecks, DoesNotHitOutsideItsBounds) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const bool hit = field.hit_test(Point{500.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(TextFieldChecks, APressPlacesTheCaretOnTheByteItLandsOver) {
    // arrange a field with a five character value, where every byte is one advance wide (§9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{34.0f, 10.0f}};
    // act the press lands past the third byte, so the caret snaps to the byte it passed (§9.1)
    field.dispatch_event(event);
    // assert
    EXPECT_TRUE(event.handled);
    EXPECT_EQ(field.caret(), 3u);
}

TEST(TextFieldChecks, APressOutsideTheFieldIsNotClaimed) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{500.0f, 10.0f}};
    // act
    field.dispatch_event(event);
    // assert a press outside the plate belongs to whatever is behind it (§8.3)
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldChecks, TheLeftArrowStepsTheCaretBack) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 2u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, TheRightArrowStepsTheCaretForward) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 3u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, TheLeftArrowDoesNotStepPastTheStart) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(0);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 0u);
}

TEST(TextFieldChecks, TheRightArrowDoesNotStepPastTheEnd) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 5u);
}

TEST(TextFieldChecks, AModifiedArrowIsAnApplicationShortcut) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    event.modifiers.control = true;
    // act
    field.dispatch_event(event);
    // assert the control arrow is an application shortcut, so the field stays out of it (§8.3, §18.5)
    EXPECT_FALSE(event.handled);
    EXPECT_EQ(field.caret(), 3u);
}

TEST(TextFieldChecks, HomePutsTheCaretAtTheStart) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(4);
    Event event{.kind = EventKind::KeyDown, .key = Key::Home};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 0u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, EndPutsTheCaretAtTheEnd) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(1);
    Event event{.kind = EventKind::KeyDown, .key = Key::End};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 5u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, TextAppearsAtTheCaret) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Alb");
    field.set_caret(3);
    Event event{.kind = EventKind::TextInput, .text = "or"};
    // act
    field.dispatch_event(event);
    // assert the commit REPLACED nothing, it inserted at the open span (§8.3)
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_EQ(field.caret(), 5u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, AnEditMarksTheFieldForBothPasses) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Alb");
    field.clear_dirty();
    Event event{.kind = EventKind::TextInput, .text = "o"};
    // act
    field.dispatch_event(event);
    // assert a text change resizes the plate, so both layout passes must run (§8.2)
    EXPECT_TRUE(field.measure_dirty());
    EXPECT_TRUE(field.render_dirty());
}

TEST(TextFieldChecks, BackspaceRemovesTheCharacterBeforeTheCaret) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "Albo");
    EXPECT_EQ(field.caret(), 4u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, BackspaceOnAnEmptyValueRemovesNothing) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert an edit that lands on nothing is not an answer (§8.3)
    EXPECT_TRUE(field.value().empty());
    EXPECT_EQ(field.caret(), 0u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, BackspaceRemovesAWholeMultiByteCharacter) {
    // arrange a value of one two byte character, whose steps land on the boundaries (§9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "\xC3\xA9");
    field.set_caret(2);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert both bytes went at once, because a step is over a character (§9.1)
    EXPECT_EQ(field.value(), "");
    EXPECT_EQ(field.caret(), 0u);
}

TEST(TextFieldChecks, DeleteRemovesTheCharacterAtTheCaret) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    Event event{.kind = EventKind::KeyDown, .key = Key::Delete};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "Alor");
    EXPECT_EQ(field.caret(), 2u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, DeleteAtTheEndRemovesNothing) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::Delete};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_EQ(field.caret(), 5u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, EnterFiresTheCommitHandler) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    std::string committed;
    field.set_on_commit([&committed](const std::string& value) { committed = value; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert the commit carries the value the field holds (§8)
    EXPECT_EQ(committed, "Albor");
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, AReturnDoesNotFireTheChangeHandler) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    std::size_t changes = 0;
    field.set_on_change([&changes](const std::string&) { changes++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert a commit fires on the explicit signal, and nothing else (§8)
    EXPECT_EQ(changes, 0u);
}

TEST(TextFieldChecks, TheChangeHandlerReceivesTheNewValue) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Alb");
    field.set_caret(3);
    std::string changed;
    field.set_on_change([&changed](const std::string& value) { changed = value; });
    Event event{.kind = EventKind::TextInput, .text = "or"};
    // act
    field.dispatch_event(event);
    // assert the handler reads the value that was kept, not the one it replaced (§8)
    EXPECT_EQ(changed, "Albor");
}

TEST(TextFieldChecks, AnEditWithoutAHandlerStillEdits) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Alb");
    field.set_caret(3);
    Event event{.kind = EventKind::TextInput, .text = "or"};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "Albor");
}

TEST(TextFieldChecks, AReadOnlyFieldRefusesText) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    Event event{.kind = EventKind::TextInput, .text = "x"};
    // act
    field.dispatch_event(event);
    // assert a read only field refuses every edit (§8.3)
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldChecks, AReadOnlyFieldRefusesBackspace) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldChecks, AReadOnlyFieldStillMovesItsCaret) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    field.dispatch_event(event);
    // assert a read only field still takes focus and still permits selection, so it still moves (§8.3)
    EXPECT_EQ(field.caret(), 4u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, AReadOnlyFieldStillCommits) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    std::string committed;
    field.set_on_commit([&committed](const std::string& value) { committed = value; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert a commit is a signal, not an edit, so a read only field still raises it (§8)
    EXPECT_EQ(committed, "Albor");
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldChecks, AReadOnlyFieldIsStillFocusable) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const bool focusable = FocusManager::focusable(field);
    // assert read only is not disabled, because the two refuse different things (§8.3)
    EXPECT_TRUE(focusable);
}

TEST(TextFieldChecks, ADisabledFieldIsNotFocusable) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_disabled(true);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const bool focusable = FocusManager::focusable(field);
    // assert a disabled field refuses the ring, because it refuses everything (§8.3)
    EXPECT_FALSE(focusable);
}

TEST(TextFieldChecks, ADisabledFieldRefusesText) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_disabled(true);
    Event event{.kind = EventKind::TextInput, .text = "x"};
    // act
    field.dispatch_event(event);
    // assert a disabled field takes no text, because it refuses everything (§18.7)
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldChecks, ADisabledFieldRefusesTheCaretKeys) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_disabled(true);
    field.set_caret(5);
    Event left{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    field.dispatch_event(left);
    // assert
    EXPECT_FALSE(left.handled);
    EXPECT_EQ(field.caret(), 5u);
}

TEST(TextFieldChecks, AnnouncesTheTextFieldRoleAndItsIdentity) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor", "name_field");
    field.arrange(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{120.0f, 20.0f}));
    // act
    const SemanticNode node = field.semantic_node();
    // assert the role is the one the bridge maps to a text field (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::TextField);
    EXPECT_EQ(node.name(), "name_field");
    EXPECT_EQ(node.value(), "Albor");
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{120.0f, 20.0f})));
}

TEST(TextFieldChecks, TheReadOnlyStateReachesTheSemanticNode) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor", "name_field");
    field.set_read_only(true);
    // act
    const SemanticNode node = field.semantic_node();
    // assert
    EXPECT_TRUE(node.read_only());
    EXPECT_FALSE(node.disabled());
}

TEST(TextFieldChecks, TheDisabledStateReachesTheSemanticNode) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor", "name_field");
    field.set_disabled(true);
    // act
    const SemanticNode node = field.semantic_node();
    // assert
    EXPECT_TRUE(node.disabled());
    EXPECT_FALSE(node.read_only());
}

TEST(TextFieldChecks, AFieldHasNoCheckState) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor", "name_field");
    // act
    const SemanticNode node = field.semantic_node();
    // assert a field is not checkable, because it carries no boolean (§10.1)
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
}

TEST(TextFieldChecks, AnBoundCommandOwnsTheDisabledState) {
    // arrange a field bound to a command that publishes enablement (§18.7)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    Command command("commit");
    command.set_enabled(false);
    field.set_command(&command);
    // act
    field.set_disabled(true);
    // assert a bound command wins, which is what the imperative write being recorded means (§18.7)
    EXPECT_TRUE(field.disabled());
    // the command is enabled, and the field reads the single answer (§18.7)
    command.set_enabled(true);
    EXPECT_FALSE(field.disabled());
}

TEST(TextFieldChecks, AThemeResolvesTheFieldRoles) {
    // arrange a theme that overrides the field text and caret colours (§6.1)
    EditingTextEngine engine;
    Theme theme;
    theme.set_text_field_text(Color::rgb(1.0f, 0.0f, 0.0f));
    theme.set_text_field_caret(Color::rgb(0.0f, 1.0f, 0.0f));
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    field.set_focused(true);
    field.set_theme(&theme);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the caret resolves the role the theme owns, which is why the override reaches the scene (§6.1)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_GE(rects.size(), 1u);
    for (const Rect& rect : rects)
        EXPECT_FLOAT_EQ(rect.width, 1.0f);
}

TEST(TextFieldChecks, TheMetricsOverlayAnswersThroughTheRun) {
    // arrange a field whose run the engine shaped one glyph per byte (§9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const float ascent = field.line_ascent();
    const float descent = field.line_descent();
    const Rect caret = field.caret_rect();
    // assert the overlay reads the pair the run was measured at (§9.1)
    EXPECT_FLOAT_EQ(ascent, 12.8f);
    EXPECT_FLOAT_EQ(descent, 3.2f);
    // assert the caret stands two advances in and past the inset, which is what its byte maps to (§9.1)
    EXPECT_FLOAT_EQ(caret.x, 4.0f + 20.0f);
    EXPECT_FLOAT_EQ(caret.width, 1.0f);
}

TEST(TextFieldSelectionChecks, ASelectionSpansBetweenTheTwoEnds) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(5);
    // act
    field.set_selection(1, 4);
    // assert the ordered pair reads from the low end to the high one, whichever way it was written (§8.3)
    EXPECT_TRUE(field.has_selection());
    EXPECT_EQ(field.selection_start(), 1u);
    EXPECT_EQ(field.selection_end(), 4u);
}

TEST(TextFieldSelectionChecks, ASelectionWrittenBackwardsStillReadsForward) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    // act
    field.set_selection(4, 1);
    // assert the ends are ordered by their bytes, not by the order they arrived (§8.3)
    EXPECT_EQ(field.selection_start(), 1u);
    EXPECT_EQ(field.selection_end(), 4u);
}

TEST(TextFieldSelectionChecks, AnEqualPairIsNoSelection) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    // act
    field.set_selection(2, 2);
    // assert a degenerate span is a placement, which is what collapse means (§8.3)
    EXPECT_FALSE(field.has_selection());
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldSelectionChecks, SelectAllSpansTheWholeValue) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    // act
    field.select_all();
    // assert the span is the value itself (§8.3)
    EXPECT_EQ(field.selection_start(), 0u);
    EXPECT_EQ(field.selection_end(), 5u);
}

TEST(TextFieldSelectionChecks, SelectAllOnAnEmptyValueStaysCollapsed) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    // act
    field.select_all();
    // assert there is nothing to span, which is what an empty field gives (§8.3)
    EXPECT_FALSE(field.has_selection());
}

TEST(TextFieldSelectionChecks, AShiftedArrowExtendsTheSelection) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    event.modifiers.shift = true;
    // act
    field.dispatch_event(event);
    // assert the anchor stays and the caret extends, which is what shift means (§8.3)
    EXPECT_TRUE(field.has_selection());
    EXPECT_EQ(field.selection_start(), 3u);
    EXPECT_EQ(field.selection_end(), 4u);
}

TEST(TextFieldSelectionChecks, AShiftedArrowExtendsBackwards) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    event.modifiers.shift = true;
    // act
    field.dispatch_event(event);
    // assert the span reads forward however the caret moved (§8.3)
    EXPECT_EQ(field.selection_start(), 2u);
    EXPECT_EQ(field.selection_end(), 3u);
}

TEST(TextFieldSelectionChecks, ASelectionDuringATypingEditIsReplaced) {
    // arrange a field with a selection the next commit must swap out (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 4);
    Event event{.kind = EventKind::TextInput, .text = "e"};
    // act
    field.dispatch_event(event);
    // assert the span was replaced by what was committed (§8.3)
    EXPECT_EQ(field.value(), "Aer");
    EXPECT_EQ(field.caret(), 2u);
    EXPECT_FALSE(field.has_selection());
}

TEST(TextFieldSelectionChecks, ABackspaceRemovesTheWholeSelection) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert the span went whole, which is what backspacing a selection means (§8.3)
    EXPECT_EQ(field.value(), "Ar");
    EXPECT_EQ(field.caret(), 1u);
}

TEST(TextFieldSelectionChecks, APlainArrowCollapsesToTheNearerEnd) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    field.dispatch_event(event);
    // assert the left arrow parks the caret at the start of the span (§8.3)
    EXPECT_FALSE(field.has_selection());
    EXPECT_EQ(field.caret(), 1u);
}

TEST(TextFieldSelectionChecks, APlainRightArrowCollapsesToTheFarEnd) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    field.dispatch_event(event);
    // assert the right arrow parks the caret at the end of the span (§8.3)
    EXPECT_FALSE(field.has_selection());
    EXPECT_EQ(field.caret(), 4u);
}

TEST(TextFieldSelectionChecks, APressCollapsesTheSelectedSpan) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_selection(1, 4);
    Event event{.kind = EventKind::PointerDown, .position = Point{14.0f, 10.0f}};
    // act
    field.dispatch_event(event);
    // assert a press inside the plate places the caret, which is what collapse on click means (§8.3)
    EXPECT_FALSE(field.has_selection());
}

TEST(TextFieldSelectionChecks, ADragExtendsTheSelectionWhileThePressHolds) {
    // arrange a press that was claimed, which starts the drag (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event down{.kind = EventKind::PointerDown, .position = Point{14.0f, 10.0f}};
    field.dispatch_event(down);
    // act
    Event move{.kind = EventKind::PointerMove, .position = Point{34.0f, 10.0f}};
    field.dispatch_event(move);
    // assert the anchor stayed at the press and the caret followed the pointer (§8.3)
    EXPECT_TRUE(field.has_selection());
    EXPECT_EQ(field.selection_start(), 1u);
    EXPECT_EQ(field.selection_end(), 3u);
}

TEST(TextFieldSelectionChecks, AReleaseEndsTheDrag) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event down{.kind = EventKind::PointerDown, .position = Point{14.0f, 10.0f}};
    field.dispatch_event(down);
    Event move{.kind = EventKind::PointerMove, .position = Point{34.0f, 10.0f}};
    field.dispatch_event(move);
    // act
    Event up{.kind = EventKind::PointerUp, .position = Point{34.0f, 10.0f}};
    field.dispatch_event(up);
    Event move_after{.kind = EventKind::PointerMove, .position = Point{54.0f, 10.0f}};
    field.dispatch_event(move_after);
    // assert a move after the release extends nothing, because the drag is over (§8.3)
    EXPECT_EQ(field.selection_end(), 3u);
}

TEST(TextFieldSelectionChecks, ASelectionPaintsAHighlight) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 3);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the highlight and the caret bar are the two rect kinds, and no caret inside a span (§6.1)
    for (const Rect& rect : rect_commands(*recorder.scene()))
        EXPECT_GE(rect.width, 1.0f);
}

TEST(TextFieldClipboardChecks, CopyPlacesTheSelectionOnTheClipboard) {
    // arrange a field over a clipboard the test holds (§3.5)
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    // act
    field.copy();
    // assert the span is what the clipboard carries (§3.5)
    EXPECT_EQ(clipboard.text(), "lbo");
}

TEST(TextFieldClipboardChecks, CopyOnAnEmptySelectionLeavesTheClipboardAlone) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "before";
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    // act
    field.copy();
    // assert a spanless field has nothing to hand over (§3.5)
    EXPECT_EQ(clipboard.text(), "before");
}

TEST(TextFieldClipboardChecks, AReadOnlyFieldStillCopies) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    // act
    field.copy();
    // assert read only refuses edits, which copy is not (§8.3)
    EXPECT_EQ(clipboard.text(), "lbo");
}

TEST(TextFieldClipboardChecks, CutRemovesTheSelectionAndCopiesIt) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    // act
    field.cut();
    // assert the value kept what the clipboard took (§3.5)
    EXPECT_EQ(clipboard.text(), "lbo");
    EXPECT_EQ(field.value(), "Ar");
    EXPECT_EQ(field.caret(), 1u);
}

TEST(TextFieldClipboardChecks, AReadOnlyFieldRefusesCut) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    // act
    field.cut();
    // assert
    EXPECT_EQ(field.value(), "Albor");
    EXPECT_TRUE(clipboard.text().empty());
}

TEST(TextFieldClipboardChecks, PasteInsertsAtTheCaret) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "XYZ";
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_caret(3);
    // act
    field.paste();
    // assert
    EXPECT_EQ(field.value(), "AlbXYZor");
    EXPECT_EQ(field.caret(), 6u);
}

TEST(TextFieldClipboardChecks, PasteReplacesTheSelection) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "XYZ";
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    // act
    field.paste();
    // assert the span was the three bytes, which the remainder kept (§3.5)
    EXPECT_EQ(field.value(), "AXYZr");
    EXPECT_EQ(field.caret(), 4u);
}

TEST(TextFieldClipboardChecks, PasteStripsTheLineBreaksOfASingleLineField) {
    // arrange a clipboard that carries a multi line run (§8.3)
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "x\ny\rc";
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_caret(0);
    // act
    field.paste();
    // assert a single line field edits one line, so the breaks leave (§8.3)
    EXPECT_EQ(field.value(), "xycAlbor");
}

TEST(TextFieldClipboardChecks, AReadOnlyFieldRefusesPaste) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "XYZ";
    TextField field(nullptr, &engine, "Albor");
    field.set_read_only(true);
    field.set_clipboard(&clipboard);
    // act
    field.paste();
    // assert
    EXPECT_EQ(field.value(), "Albor");
}

TEST(TextFieldClipboardChecks, AFieldWithNoClipboardDropsTheCopy) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_selection(1, 4);
    // act
    field.copy();
    field.cut();
    field.paste();
    // assert nothing wrote and nothing crashed, which is what the absent surface is (§3.5)
    EXPECT_EQ(field.value(), "Albor");
}

TEST(TextFieldKeyboardChecks, TheCopyKeyCopies) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::C};
    event.modifiers.command = true;
    // act
    field.dispatch_event(event);
    // assert the chord the registry declined reaches the field (§18.5, §18)
    EXPECT_EQ(clipboard.text(), "lbo");
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldKeyboardChecks, TheControlCopyKeyCopiesToo) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::C};
    event.modifiers.control = true;
    // act
    field.dispatch_event(event);
    // assert the editor modifier is one of the two, which is the portable answer (§18.5)
    EXPECT_EQ(clipboard.text(), "lbo");
}

TEST(TextFieldKeyboardChecks, AnOptionChordIsNeverClaimed) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_selection(1, 4);
    Event event{.kind = EventKind::KeyDown, .key = Key::C};
    event.modifiers.option = true;
    // act
    field.dispatch_event(event);
    // assert an option chord is platform text, which no editing binding takes (§18.5)
    EXPECT_FALSE(event.handled);
    EXPECT_TRUE(clipboard.text().empty());
}

TEST(TextFieldKeyboardChecks, ThePasteKeyPastes) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "XYZ";
    TextField field(nullptr, &engine, "Albor");
    field.set_clipboard(&clipboard);
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::V};
    event.modifiers.command = true;
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.value(), "AlbXYZor");
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldKeyboardChecks, TheSelectAllKeySpansTheValue) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    Event event{.kind = EventKind::KeyDown, .key = Key::A};
    event.modifiers.command = true;
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_TRUE(field.has_selection());
    EXPECT_EQ(field.selection_start(), 0u);
    EXPECT_EQ(field.selection_end(), 5u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldKeyboardChecks, TheUndoKeyReversesOneEdit) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "Alb");
    field.bind_undo_stack(&stack);
    field.set_caret(3);
    Event insert{.kind = EventKind::TextInput, .text = "o"};
    field.dispatch_event(insert);
    Event event{.kind = EventKind::KeyDown, .key = Key::Z};
    event.modifiers.command = true;
    // act
    field.dispatch_event(event);
    // assert the reversal restored the value the edit replaced (§18)
    EXPECT_EQ(field.value(), "Alb");
    EXPECT_TRUE(field.render_dirty());
}

TEST(TextFieldKeyboardChecks, TheUndoKeyWithShiftReappliesOneEdit) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "Alb");
    field.bind_undo_stack(&stack);
    field.set_caret(3);
    Event insert{.kind = EventKind::TextInput, .text = "o"};
    field.dispatch_event(insert);
    Event undo{.kind = EventKind::KeyDown, .key = Key::Z};
    undo.modifiers.command = true;
    field.dispatch_event(undo);
    Event redo{.kind = EventKind::KeyDown, .key = Key::Z};
    redo.modifiers.command = true;
    redo.modifiers.shift = true;
    // act
    field.dispatch_event(redo);
    // assert the reapply restored what the reversal removed (§18)
    EXPECT_EQ(field.value(), "Albo");
}

TEST(TextFieldUndoChecks, AnEditRecordsOneUndoEntry) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine);
    field.bind_undo_stack(&stack);
    // act
    field.set_caret(0);
    Event insert{.kind = EventKind::TextInput, .text = "a"};
    field.dispatch_event(insert);
    // assert one edit is one entry, which depth reads (§18)
    EXPECT_EQ(stack.depth(), 1u);
}

TEST(TextFieldUndoChecks, AnUndoRestoresTheValueAndTheCaret) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "Ab");
    field.bind_undo_stack(&stack);
    field.set_caret(2);
    Event insert{.kind = EventKind::TextInput, .text = "c"};
    field.dispatch_event(insert);
    // act
    field.undo();
    // assert the snapshot carried both halves of the state (§18)
    EXPECT_EQ(field.value(), "Ab");
    EXPECT_EQ(field.caret(), 2u);
    EXPECT_FALSE(field.has_selection());
}

TEST(TextFieldUndoChecks, ARedoReappliesTheReversedEdit) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "Ab");
    field.bind_undo_stack(&stack);
    field.set_caret(2);
    Event insert{.kind = EventKind::TextInput, .text = "c"};
    field.dispatch_event(insert);
    field.undo();
    // act
    field.redo();
    // assert
    EXPECT_EQ(field.value(), "Abc");
    EXPECT_EQ(field.caret(), 3u);
}

TEST(TextFieldUndoChecks, ABackspaceRunIsOneEntryPerRemoval) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "abc");
    field.bind_undo_stack(&stack);
    field.set_caret(3);
    Event first{.kind = EventKind::KeyDown, .key = Key::Backspace};
    field.dispatch_event(first);
    Event second{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(second);
    // assert two removals are two entries, because each was an edit of its own (§18)
    EXPECT_EQ(stack.depth(), 2u);
}

TEST(TextFieldUndoChecks, AFieldWithNoStackStillEdits) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Ab");
    field.set_caret(2);
    Event insert{.kind = EventKind::TextInput, .text = "c"};
    // act
    field.dispatch_event(insert);
    // assert an edit without history still lands, which is what an absent stack forgives (§18)
    EXPECT_EQ(field.value(), "Abc");
    EXPECT_EQ(field.undo_stack(), nullptr);
}

TEST(TextFieldUndoChecks, AReversalSignalsTheChangeHandlerOnce) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine, "Ab");
    field.bind_undo_stack(&stack);
    field.set_caret(2);
    Event insert{.kind = EventKind::TextInput, .text = "c"};
    field.dispatch_event(insert);
    std::size_t changes = 0;
    field.set_on_change([&changes](const std::string&) { changes++; });
    // act
    field.undo();
    // assert a reversal is an edit a validation reads, and it is the one edit it was (§18)
    EXPECT_EQ(changes, 1u);
}

TEST(TextFieldMultiLineChecks, AMultiLineFieldWrapsAtTheOfferedWidth) {
    // arrange a field whose value is wider than the plate it is given (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("abcdefghijklmnopqrstuvwxyz");
    // act
    Size wrapped{};
    field.measure(SizeConstraints::at_most(Size{80.0f, 400.0f}), wrapped);
    // assert the run broke at the ceiling, which is three lines of the double (§9.1)
    ASSERT_EQ(field.layout().run->lines().size(), 4u);
    // assert every line spans its own bytes, which is what the caret maps against (§9.1)
    EXPECT_EQ(field.layout().run->lines().front().start_byte, 0u);
    EXPECT_EQ(field.layout().run->lines()[1].start_byte, 7u);
}

TEST(TextFieldMultiLineChecks, AWrappedRunGrowsThePlateHeight) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("abcdefghijklmnopqrstuvwxyz");
    // act
    Size wrapped{};
    field.measure(SizeConstraints::at_most(Size{80.0f, 400.0f}), wrapped);
    // assert the plate is as tall as the lines the run needs (§8.2)
    EXPECT_GE(wrapped.height, EditingTextEngine::HEIGHT * 4.0f);
}

TEST(TextFieldMultiLineChecks, ReturnInsertsABreakOnAMultiLineField) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcd");
    field.set_multiline(true);
    field.set_caret(2);
    std::size_t commits = 0;
    field.set_on_commit([&commits](const std::string&) { commits++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert the break split the value at the caret (§8)
    EXPECT_EQ(field.value(), "ab\ncd");
    EXPECT_EQ(field.caret(), 3u);
    EXPECT_EQ(commits, 0u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldMultiLineChecks, AReturnChordStillCommitsAMultiLineField) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "ab\ncd");
    field.set_multiline(true);
    std::string committed;
    field.set_on_commit([&committed](const std::string& value) { committed = value; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    event.modifiers.command = true;
    // act
    field.dispatch_event(event);
    // assert the explicit commit is the chord, which plain return is not (§8)
    EXPECT_EQ(committed, "ab\ncd");
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldMultiLineChecks, AReadOnlyMultiLineFieldRefusesTheBreak) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "ab\ncd");
    field.set_multiline(true);
    field.set_read_only(true);
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert a break is an edit, which a read only field refuses (§8.3)
    EXPECT_EQ(field.value(), "ab\ncd");
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldMultiLineChecks, TheCaretDescendsToTheLineBelow) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(1);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    field.dispatch_event(event);
    // assert the caret kept its column, which is byte one of the line below (§8.3)
    EXPECT_EQ(field.caret(), 4u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldMultiLineChecks, TheCaretAscendsToTheLineAbove) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncdefgh");
    field.set_caret(5);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    // act
    field.dispatch_event(event);
    // assert the caret kept the column as far as the line above goes, which is two bytes (§8.3)
    EXPECT_EQ(field.caret(), 2u);
    EXPECT_TRUE(event.handled);
}

TEST(TextFieldMultiLineChecks, TheColumnGoalSurvivesTheLineWalk) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd\nefghij");
    field.set_caret(4);
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    field.dispatch_event(down);
    // assert the descent landed on the byte the column goal maps to (§8.3)
    EXPECT_EQ(field.caret(), 7u);
    // act
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    field.dispatch_event(up);
    // assert the walk down and back lands on the byte it left, because the column was kept (§8.3)
    EXPECT_EQ(field.caret(), 4u);
    EXPECT_TRUE(up.handled);
}

TEST(TextFieldMultiLineChecks, AnUpOnTheFirstLineClampsToItsStart) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    // act
    field.dispatch_event(event);
    // assert the caret stepped to the start of the first line, which is what clamping gives (§8.3)
    EXPECT_EQ(field.caret(), 0u);
}

TEST(TextFieldMultiLineChecks, ADownOnTheLastLineStaysPut) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(4);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_EQ(field.caret(), 4u);
}

TEST(TextFieldMultiLineChecks, HomeAndEndParkTheCaretOnItsOwnLine) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(4);
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    // act
    field.dispatch_event(home);
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    field.dispatch_event(end);
    // assert the line is the one the key answered (§8.3)
    EXPECT_EQ(field.caret(), 5u);
}

TEST(TextFieldMultiLineChecks, ABackspaceJoinsTwoLines) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(3);
    Event event{.kind = EventKind::KeyDown, .key = Key::Backspace};
    // act
    field.dispatch_event(event);
    // assert the break went and the lines joined (§8.3)
    EXPECT_EQ(field.value(), "abcd");
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldMultiLineChecks, ABlankLineIsARealLineToTheCaret) {
    // arrange a value whose middle row is blank (§9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\n\nb");
    field.set_caret(1);
    // act
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    field.dispatch_event(down);
    const std::size_t below_blank = field.caret();
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    field.dispatch_event(up);
    // assert the blank row holds a caret stop, which the byte map places (§9.1)
    EXPECT_EQ(below_blank, 3u);
    // assert the way back to the blank row stands on the byte the row begins at (§9.1)
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldMultiLineChecks, ASelectionSpansTwoLinesAndPaintsBoth) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_selection(1, 4);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 60.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert each crossed line contributed its own highlight, which two lines are (§8.3)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    EXPECT_GE(rects.size(), 2u);
}

TEST(TextFieldMultiLineChecks, TheCaretRectFollowsTheLineItHangsOn) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(3);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 60.0f}));
    // act
    const Rect on_second = field.caret_rect();
    // assert the caret hung from the second line, which the byte resolves to (§9.1)
    EXPECT_EQ(field.caret(), 3u);
    // assert the x is the pen of the byte within its line, which the inset offsets (§9.1)
    EXPECT_FLOAT_EQ(on_second.x, 4.0f);
    // assert the bar sits on the second line box, which is a baseline below the first (§9.1)
    EXPECT_NEAR(on_second.y, 30.0f, 0.001f);
}

TEST(TextFieldMultiLineChecks, SingleLineFieldsOwnTheirHeightAndPlainReturn) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcd");
    std::string committed;
    field.set_on_commit([&committed](const std::string& value) { committed = value; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    field.dispatch_event(event);
    // assert a single line field kept the W5 answer exactly (§8)
    EXPECT_EQ(committed, "abcd");
    EXPECT_EQ(field.value(), "abcd");
}

TEST(TextFieldMultiLineChecks, ASetOnAMultiLineFieldKeepsTheBreaks) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    // act
    field.set_value("ab\ncd");
    // assert the value was taken as written, which a multi line field is for (§8)
    EXPECT_EQ(field.value(), "ab\ncd");
}

TEST(TextFieldMultiLineChecks, ASetStripsBreaksRightBackOnASingleLineField) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    // act
    field.set_value("ab\ncd");
    // assert a single line field edits one line, so the value kept no break (§8.3)
    EXPECT_EQ(field.value(), "abcd");
}

TEST(TextFieldMultiLineChecks, PasteKeepsBreaksOnAMultiLineField) {
    // arrange
    EditingTextEngine engine;
    HoldingClipboard clipboard;
    clipboard.carried = "x\ny";
    TextField field(nullptr, &engine, "ab");
    field.set_multiline(true);
    field.set_clipboard(&clipboard);
    field.set_caret(1);
    // act
    field.paste();
    // assert the break was pasted, because the field edits lines (§8.3)
    EXPECT_EQ(field.value(), "ax\nyb");
}

TEST(TextFieldMultiLineChecks, AMultiLineFieldIsAnnouncedMultiLine) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "ab\ncd", "notes");
    field.set_multiline(true);
    // act
    const SemanticNode node = field.semantic_node();
    // assert the state reaches assistive technology, which is what announces a document (§10.1)
    EXPECT_TRUE(node.multiline());
    EXPECT_EQ(node.role(), SemanticRole::TextField);
}

TEST(TextFieldMultiLineChecks, SetMultilineRedrawsAndRemeasures) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcd");
    field.clear_dirty();
    // act
    field.set_multiline(true);
    // assert both passes must run, because the plate may change size (§8.2)
    EXPECT_TRUE(field.measure_dirty());
    EXPECT_TRUE(field.render_dirty());
}

TEST(TextFieldImeChecks, ACompositionShowsTheMarkedString) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    Event composing{.kind = EventKind::Composing,
                    .text = "a\xE3\x81\x82"
                            "b"};
    field.dispatch_event(composing);
    field.render(recorder);
    // assert the mark reached the display, because the value kept holding nothing (§8.3)
    EXPECT_TRUE(field.value().empty());
    EXPECT_EQ(text_command_count(*recorder.scene()), 1u);
    EXPECT_TRUE(field.has_marked_text());
    EXPECT_EQ(field.marked_text(),
              "a\xE3\x81\x82"
              "b");
}

TEST(TextFieldImeChecks, TheMarkIsProvisionalAndSignalsNothing) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    std::size_t changes = 0;
    field.set_on_change([&changes](const std::string&) { changes++; });
    Event composing{.kind = EventKind::Composing, .text = "abc"};
    // act
    field.dispatch_event(composing);
    // assert the value is untouched and no validation ran, which is what provisional means (§8.3)
    EXPECT_TRUE(field.value().empty());
    EXPECT_EQ(changes, 0u);
    EXPECT_EQ(field.caret(), 0u);
}

TEST(TextFieldImeChecks, AnEmptyMarkRenouncesTheComposition) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event mark{.kind = EventKind::Composing, .text = "abc"};
    field.dispatch_event(mark);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    Event unmark{.kind = EventKind::Composing};
    field.dispatch_event(unmark);
    field.render(recorder);
    // assert the display went back to the value, which an empty value draws nothing of (§8.3)
    EXPECT_FALSE(field.has_marked_text());
    EXPECT_EQ(text_command_count(*recorder.scene()), 0u);
}

TEST(TextFieldImeChecks, ACommitResolvesTheMarkIntoTheValue) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    Event mark{.kind = EventKind::Composing, .text = "ab"};
    field.dispatch_event(mark);
    // act
    Event commit{.kind = EventKind::TextInput, .text = "XYZ"};
    field.dispatch_event(commit);
    // assert the final text entered at the caret and the mark is gone (§8.3)
    EXPECT_EQ(field.value(), "XYZ");
    EXPECT_EQ(field.caret(), 3u);
    EXPECT_FALSE(field.has_marked_text());
}

TEST(TextFieldImeChecks, TheDisplayRunReturnsToTheValueAfterACommit) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event mark{.kind = EventKind::Composing, .text = "x"};
    field.dispatch_event(mark);
    // act
    Event commit{.kind = EventKind::TextInput, .text = "x"};
    field.dispatch_event(commit);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert the value alone is what draws after a commit (§8.3)
    EXPECT_FALSE(field.has_marked_text());
    EXPECT_EQ(1, text_command_count(*recorder.scene()));
}

TEST(TextFieldImeChecks, ACommitWithAMarkRecordsOneUndoStep) {
    // arrange
    EditingTextEngine engine;
    UndoStack stack;
    TextField field(nullptr, &engine);
    field.bind_undo_stack(&stack);
    Event mark{.kind = EventKind::Composing, .text = "ab"};
    field.dispatch_event(mark);
    // act
    Event commit{.kind = EventKind::TextInput, .text = "XYZ"};
    field.dispatch_event(commit);
    // assert the mark cost no history and the commit is the one entry (§8.3, §18)
    EXPECT_EQ(stack.depth(), 1u);
    field.undo();
    EXPECT_TRUE(field.value().empty());
}

TEST(TextFieldImeChecks, AnEmptyFieldKeepsTheHintUnderTheMark) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_placeholder("Type here");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder before(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(before);
    // act
    Event mark{.kind = EventKind::Composing, .text = "a"};
    field.dispatch_event(mark);
    SceneRecorder after(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(after);
    // assert the hint gives way to the mark, which is what composition shows instead (§8.3)
    EXPECT_EQ(1u, text_command_count(*before.scene()));
    EXPECT_EQ(1u, text_command_count(*after.scene()));
    EXPECT_TRUE(field.has_marked_text());
}

TEST(TextFieldImeChecks, TheMarkDrawsTheUnderlineBar) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    Event mark{.kind = EventKind::Composing, .text = "abc"};
    field.dispatch_event(mark);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert the underline is the one rect the mark records (§8.3)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_GE(rects.size(), 1u);
    EXPECT_FLOAT_EQ(rects.front().height, 1.0f);
    // assert the underline spans the mark's bytes, which start at the caret (§8.3)
    EXPECT_FLOAT_EQ(rects.front().width, static_cast<float>(field.marked_text().size()) * EditingTextEngine::ADVANCE);
}

TEST(TextFieldImeChecks, AReadOnlyFieldRefusesCompose) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_read_only(true);
    Event event{.kind = EventKind::Composing, .text = "abc"};
    // act
    field.dispatch_event(event);
    // assert
    EXPECT_FALSE(field.has_marked_text());
    EXPECT_FALSE(event.handled);
}

TEST(TextFieldImeChecks, TheCaretRectStaysAtTheMarkStart) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(3);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const Rect before = field.caret_rect();
    Event mark{.kind = EventKind::Composing, .text = "XYZ"};
    field.dispatch_event(mark);
    const Rect during = field.caret_rect();
    // assert the caret held its byte, which is where the mark is being composed at (§8.3)
    EXPECT_FLOAT_EQ(during.x, before.x);
}

TEST(TextFieldImeChecks, ACompositionOverASelectionShowsWhatTheCommitWillReplace) {
    // arrange a field whose whole value is selected, which a composition lands over (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.select_all();
    Event mark{.kind = EventKind::Composing, .text = "ab"};
    field.dispatch_event(mark);
    // act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert the provisional display is the value with the selection replaced (§8.3)
    std::string painted;
    for (const DrawCommand& command : recorder.scene()->commands()) {
        if (command.kind() != DrawCommandKind::Text)
            continue;
        const TextGeometry& geometry = command.text_geometry();
        if (geometry.layout.run == nullptr)
            continue;
        // the double shapes one glyph per character, so the ids spell the run (§9.1)
        for (const ShapedGlyph& glyph : geometry.layout.run->lines().front().glyphs)
            painted += static_cast<char>(glyph.id);
    }
    EXPECT_EQ(painted, "ab");
}

TEST(TextFieldImeChecks, ACompositionOverASelectionCommitsOverTheSameSpan) {
    // arrange the same selection composed over, which is what the provisional display showed (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.select_all();
    Event mark{.kind = EventKind::Composing, .text = "ab"};
    field.dispatch_event(mark);
    Event commit{.kind = EventKind::TextInput, .text = "ab"};
    // act
    field.dispatch_event(commit);
    // assert the value is the composed string alone, because the replaced span went with it (§8.3)
    EXPECT_EQ(field.value(), "ab");
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldChecks, ACommittedTextCarryingALineBreakIsStrippedByASingleLineField) {
    // arrange a field that edits one line, whatever the committing input method hands it (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    Event commit{.kind = EventKind::TextInput, .text = "a\nb\rc"};
    // act
    field.dispatch_event(commit);
    // assert the breaks are gone, because the field has no row to put them on (§8.3)
    EXPECT_EQ(field.value(), "abc");
    EXPECT_EQ(field.caret(), 3u);
}

TEST(TextFieldMultiLineChecks, ACommittedTextCarryingALineBreakIsKeptByAMultiLineField) {
    // arrange a field that edits rows, which is where a break is a character (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    Event commit{.kind = EventKind::TextInput, .text = "a\nb"};
    // act
    field.dispatch_event(commit);
    // assert the break stayed, because the field edits more than one line (§8.3)
    EXPECT_EQ(field.value(), "a\nb");
    EXPECT_EQ(field.layout().run->lines().size(), 2u);
}

TEST(TextFieldUndoChecks, AReversalCannotRestoreALineBreakIntoASingleLineField) {
    // arrange an edit recorded in a field that edits one line (§18, §8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "ab");
    UndoStack stack;
    field.bind_undo_stack(&stack);
    Event commit{.kind = EventKind::TextInput, .text = "\n"};
    field.dispatch_event(commit);
    // act
    field.undo();
    // assert the reversed value holds no break, because the invariant belongs to the field rather than to one path (§8.3)
    EXPECT_EQ(field.value(), "ab");
    EXPECT_EQ(field.layout().run->lines().size(), 1u);
}

TEST(TextFieldChecks, TheOverlayXIsThePositionOfTheOffsetAndNotAnAverage) {
    // arrange a field measured with proportional advances, where a prefix is not the mean (§9.1)
    ProportionalTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 20.0f}));
    // act
    const float at_first = field.x_at_byte(1);
    const float at_second = field.x_at_byte(2);
    const float at_end = field.x_at_byte(3);
    // assert the first offset sits past the wide glyph, which an average of the whole run would not (§9.1)
    EXPECT_FLOAT_EQ(at_first, 2.0f * ProportionalTextEngine::ADVANCE);
    // assert the second offset adds only that character's own advance (§9.1)
    EXPECT_FLOAT_EQ(at_second, 3.0f * ProportionalTextEngine::ADVANCE);
    // assert the end of the value sits at the line advance, the last right edge (§9.1)
    EXPECT_FLOAT_EQ(at_end, 4.0f * ProportionalTextEngine::ADVANCE);
    // assert the mean of the run is not what it reports, because that is the mistake (§9.1)
    EXPECT_NE(at_first, 40.0f / 3.0f);
}

TEST(TextFieldChecks, TheOverlayXIsZeroWhereThereIsNoRun) {
    // arrange a field the engine could not measure, which has no position to report (§9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 20.0f}));
    engine.usable = false;
    field.set_value("abcd");
    // act, assert
    EXPECT_FLOAT_EQ(field.x_at_byte(2), 0.0f);
}

TEST(TextFieldChecks, AFocusedFieldWithAnEngineStartsABlinkThatTheEngineRuns) {
    // arrange a field that was handed an engine, which is how a blink has a clock (§7.3)
    EditingTextEngine text;
    AnimationEngine animations;
    TextField field(nullptr, &text, "Albor");
    field.set_animation_engine(&animations);
    field.set_caret(2);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act the focus arrives
    field.set_focused(true);
    // assert a blink is running, because a caret that never blinks is a bar (§7.3, §8.3)
    EXPECT_EQ(animations.active_count(), 1u);
    EXPECT_TRUE(field.shows_caret());
    // act the first half of the pass goes by
    (void)animations.advance(0.26f);
    // assert the caret is still shown, because the visible half runs to the middle (§8.3)
    EXPECT_TRUE(field.shows_caret());
    // act the middle goes by
    (void)animations.advance(0.02f);
    // assert the caret is hidden, which is the other half of the rhythm (§8.3)
    EXPECT_FALSE(field.shows_caret());
    // act the pass ends
    (void)animations.advance(0.25f);
    (void)animations.advance(0.001f);
    // assert the caret is back, because the blink repeats rather than ending (§7.3)
    EXPECT_TRUE(field.shows_caret());
}

TEST(TextFieldChecks, ACaretBlinkDoesNotToggleOncePerFrame) {
    // arrange a blinking field, counting both its visibility changes and the frames it asked for (§8.3)
    EditingTextEngine text;
    AnimationEngine animations;
    TextField field(nullptr, &text, "Albor");
    field.set_animation_engine(&animations);
    std::size_t frames_asked = 0;
    field.set_invalidation_handler([&frames_asked] { frames_asked++; });
    field.set_focused(true);
    frames_asked = 0;
    std::size_t turnovers = 0;
    // the state the caret starts in, which a strobe would change on the very first frame (§8.3)
    bool previous = field.shows_caret();
    // act a second of frames at sixty hertz, which is what a window presents (§6.1)
    for (int frame = 0; frame < 60; frame++) {
        (void)animations.advance(1.0f / 60.0f);
        // assert the visibility is sampled per frame, because the claim is about what a frame does (§8.3)
        const bool now = field.shows_caret();
        // a change is counted rather than asserted per frame, because the rhythm decides when one happens (§8.3)
        if (now != previous)
            turnovers++;
        previous = now;
    }
    // assert the caret turned over about twice in a second rather than sixty times, which is the strobe (§8.3)
    EXPECT_GE(turnovers, 1u);
    EXPECT_LE(turnovers, 3u);
    // assert the frames asked for match the turnovers, because a frame that changes nothing asks for nothing (§6.1)
    EXPECT_LE(frames_asked, 3u);
}

TEST(TextFieldChecks, AFieldDestroyedWhileBlinkingLeavesNoAnimationBehind) {
    // arrange a focused field that owns a running blink, inside a scope that ends (§7.3)
    EditingTextEngine text;
    AnimationEngine animations;
    {
        TextField field(nullptr, &text, "Albor");
        field.set_animation_engine(&animations);
        field.set_focused(true);
        // assert the blink is running before the scope ends, because that is the hazard (§7.3)
        ASSERT_EQ(animations.active_count(), 1u);
    }
    // assert the engine holds nothing, because a callback into a dead field would be called on the next frame (§7.3)
    EXPECT_EQ(animations.active_count(), 0u);
    // act a frame runs over the engine that outlived the field
    EXPECT_EQ(animations.advance(0.1f), 0u);
}

TEST(TextFieldChecks, LosingTheFocusCancelsTheBlink) {
    // arrange a focused field with a running blink (§7.3)
    EditingTextEngine text;
    AnimationEngine animations;
    TextField field(nullptr, &text, "Albor");
    field.set_animation_engine(&animations);
    field.set_focused(true);
    // act the focus leaves
    field.set_focused(false);
    // assert the engine holds nothing, because a blink without a caret would spin (§7.3)
    EXPECT_EQ(animations.active_count(), 0u);
    EXPECT_FALSE(field.shows_caret());
}

TEST(TextFieldChecks, AFieldWithNoEngineShowsASolidCaret) {
    // arrange a field with no clock at all, which is what a headless or a test tree has (§7.3)
    EditingTextEngine text;
    TextField field(nullptr, &text, "Albor");
    field.set_caret(2);
    // act
    field.set_focused(true);
    // assert the caret is visible, because a field with no engine cannot blink (§8.3)
    EXPECT_TRUE(field.shows_caret());
    EXPECT_FALSE(field.focused() == false);
}

TEST(TextFieldChecks, ACaretMoveBringsTheBlinkBackIntoItsVisiblePhase) {
    // arrange a field whose caret has faded, which is where a blink leaves it (§8.3)
    EditingTextEngine text;
    AnimationEngine animations;
    TextField field(nullptr, &text, "Albor");
    field.set_animation_engine(&animations);
    field.set_focused(true);
    (void)animations.advance(0.53f);
    ASSERT_FALSE(field.shows_caret());
    // act the caret moves
    field.set_caret(3);
    // assert the caret is back, because typing and moving hold it solid (§8.3)
    EXPECT_TRUE(field.shows_caret());
    // assert exactly one blink runs, because a move restarts the phase rather than adding one (§7.3)
    EXPECT_EQ(animations.active_count(), 1u);
}

TEST(TextFieldMultiLineChecks, AClickOnTheSecondRowPutsTheCaretOnThatRow) {
    // arrange a field over two rows, arranged so the second row has a known band (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // act a press on the second row, which the double draws one height below the first (§9.1)
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 8.0f, .y = 24.0f}};
    field.dispatch_event(press);
    // assert the caret landed on the second row's first character rather than the first row's (§8.3)
    EXPECT_EQ(field.caret(), 3u);
    EXPECT_TRUE(press.handled);
}

TEST(TextFieldMultiLineChecks, AClickBelowTheLastRowClampsToTheLastRow) {
    // arrange the same field, with the plate ending below the second row (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // act a press past the end of the text (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 100.0f, .y = 39.0f}};
    field.dispatch_event(press);
    // assert the caret clamped to the end of the last row rather than resolving past the value (§8.3)
    EXPECT_EQ(field.caret(), 5u);
}

TEST(TextFieldMultiLineChecks, ADragFromTheFirstRowOntoTheSecondExtendsTheSelection) {
    // arrange a press on the first row, which anchors the selection (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 4.0f}};
    field.dispatch_event(press);
    // act the drag crosses onto the second row, which is what a vertical drag means (§8.3)
    Event drag{.kind = EventKind::PointerMove, .position = Point{.x = 26.0f, .y = 24.0f}};
    field.dispatch_event(drag);
    // assert the selection spans both rows, because the caret followed the pointer across the break (§8.3)
    EXPECT_EQ(field.selection_start(), 0u);
    EXPECT_EQ(field.selection_end(), 5u);
    // assert the drag claimed nothing else, because the field is the one being dragged (§8.3)
    Event release{.kind = EventKind::PointerUp, .position = Point{.x = 26.0f, .y = 24.0f}};
    field.dispatch_event(release);
    EXPECT_FALSE(release.handled);
}

TEST(TextFieldMultiLineChecks, AClickAboveTheFirstRowClampsToTheFirstRow) {
    // arrange a press over the plate's top edge, which belongs to the first row (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 0.0f}};
    field.dispatch_event(press);
    // assert the caret stayed on the first row rather than resolving nowhere (§8.3)
    EXPECT_EQ(field.caret(), 0u);
}

TEST(TextFieldChecks, TheSemanticNodeExposesTheSelectionAndCaretActions) {
    // arrange a field a reader can drive, which is what the actions are for (§10.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    // act
    const SemanticNode node = field.semantic_node();
    // assert the four caret moves and select all are there, because the keys that make them are (§10.1)
    EXPECT_TRUE(node.has_action("select_all"));
    EXPECT_TRUE(node.has_action("move_caret_left"));
    EXPECT_TRUE(node.has_action("move_caret_right"));
    EXPECT_TRUE(node.has_action("move_caret_up"));
    EXPECT_TRUE(node.has_action("move_caret_down"));
}

TEST(TextFieldChecks, TheSelectAllActionSelectsTheWholeValue) {
    // arrange a field with no selection, which the action creates (§10.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.set_caret(1);
    // act
    const bool invoked = field.semantic_node().invoke("select_all");
    // assert the selection covers the value, because that is what select all means (§8.3)
    EXPECT_TRUE(invoked);
    EXPECT_TRUE(field.has_selection());
    EXPECT_EQ(field.selection_start(), 0u);
    EXPECT_EQ(field.selection_end(), 3u);
}

TEST(TextFieldChecks, TheCaretActionsMoveTheCaretTheWayTheKeysDo) {
    // arrange a caret in the middle of the value (§10.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.set_caret(2);
    // act the right action, then the left one
    EXPECT_TRUE(field.semantic_node().invoke("move_caret_right"));
    EXPECT_EQ(field.caret(), 3u);
    EXPECT_TRUE(field.semantic_node().invoke("move_caret_left"));
    // assert the caret walked the bytes the keys walk (§8.3)
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldChecks, TheVerticalCaretActionsClampAtTheEndsOfTheValue) {
    // arrange a single line value, where up and down have nowhere to go (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.set_caret(2);
    // act
    EXPECT_TRUE(field.semantic_node().invoke("move_caret_down"));
    // assert the caret stayed where it was, because there is no row below (§8.3)
    EXPECT_EQ(field.caret(), 2u);
}

TEST(TextFieldChecks, TheVerticalCaretActionsWalkTheRowsOfAMultiLineValue) {
    // arrange a field over two rows, which the vertical actions climb (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.set_caret(1);
    // act
    EXPECT_TRUE(field.semantic_node().invoke("move_caret_down"));
    // assert the caret descended into the second row (§8.3)
    EXPECT_EQ(field.caret(), 4u);
}

TEST(TextFieldClipboardChecks, AFieldWithNoClipboardOfItsOwnCopiesThroughTheProcessOne) {
    // arrange a platform clipboard installed behind the factory, which is how an application arrives (§3.5)
    set_clipboard_factory([] { return std::make_unique<HoldingClipboard>(); });
    EditingTextEngine engine;
    // the field was handed no clipboard at all, which is what a generated field looks like (§8)
    TextField field(nullptr, &engine, "abcdef");
    field.select_all();
    // act
    field.copy();
    // assert the text reached the process surface, because a field with no clipboard of its own still copies (§3.5)
    Clipboard* surface = process_clipboard();
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->text(), "abcdef");
    // act again the platform goes away, so the next test sees no surface
    set_clipboard_factory(nullptr);
}

TEST(TextFieldClipboardChecks, AFieldWithNoClipboardOfItsOwnPastesThroughTheProcessOne) {
    // arrange a platform clipboard holding text, which a paste reads (§3.5)
    set_clipboard_factory([] { return std::make_unique<HoldingClipboard>(); });
    process_clipboard()->set_text("pasted");
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "ab");
    field.set_caret(2);
    // act
    field.paste();
    // assert the value took the clipboard text, because a generated field can paste without wiring (§3.5)
    EXPECT_EQ(field.value(), "abpasted");
    set_clipboard_factory(nullptr);
}

TEST(TextFieldClipboardChecks, AHandedClipboardWinsOverTheProcessOne) {
    // arrange both a process clipboard and one of the field's own, which is what a test installs (§3.5)
    set_clipboard_factory([] { return std::make_unique<HoldingClipboard>(); });
    process_clipboard()->set_text("process");
    HoldingClipboard own;
    own.carried = "own";
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_clipboard(&own);
    field.set_value("xy");
    field.select_all();
    // act
    field.copy();
    // assert the copy went to the surface the field was handed, because an explicit surface wins (§3.5)
    EXPECT_EQ(own.carried, "xy");
    EXPECT_EQ(process_clipboard()->text(), "process");
    set_clipboard_factory(nullptr);
}

TEST(TextFieldImeChecks, ThePublishedSelectionIsInUtf16Units) {
    // arrange a field holding a two byte character, which counts as one unit (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "\u00E9ab");
    field.set_selection(0, 2);
    // act
    const TextInputRange range = field.selection_range();
    // assert the range speaks the unit an input method addresses the document in (§8.3)
    EXPECT_EQ(range.location, 0u);
    EXPECT_EQ(range.length, 1u);
}

TEST(TextFieldImeChecks, ACollapsedSelectionPublishesACaret) {
    // arrange a field with no selection, which is a caret at its own byte (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    field.set_caret(2);
    // act
    const TextInputRange range = field.selection_range();
    // assert the range is a caret rather than a selection (§8.3)
    EXPECT_EQ(range.location, 2u);
    EXPECT_EQ(range.length, 0u);
}

TEST(TextFieldImeChecks, ACommitReplacesTheRangeTheInputSystemNamed) {
    // arrange a field with no selection of its own, so only the named range can place the text (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcdef");
    field.set_caret(0);
    Event commit{.kind = EventKind::TextInput, .text = "X", .replacement = TextInputRange{.location = 2, .length = 2}};
    // act
    field.dispatch_event(commit);
    // assert the named span went, which is what a reconversion means (§8.3)
    EXPECT_EQ(field.value(), "abXef");
    EXPECT_EQ(field.caret(), 3u);
}

TEST(TextFieldImeChecks, ACompositionShowsOverTheRangeTheCommitWillReplace) {
    // arrange the same named range, composed over (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcdef");
    field.set_caret(0);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 20.0f}));
    Event mark{.kind = EventKind::Composing, .text = "Z", .replacement = TextInputRange{.location = 2, .length = 2}};
    // act
    field.dispatch_event(mark);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert the provisional display replaced the named span, so it matches what the commit will do (§8.3)
    std::string painted;
    for (const DrawCommand& command : recorder.scene()->commands()) {
        if (command.kind() != DrawCommandKind::Text)
            continue;
        const TextGeometry& geometry = command.text_geometry();
        if (geometry.layout.run == nullptr)
            continue;
        for (const ShapedGlyph& glyph : geometry.layout.run->lines().front().glyphs)
            painted += static_cast<char>(glyph.id);
    }
    EXPECT_EQ(painted, "abZef");
}

TEST(TextFieldImeChecks, ANamedRangeIsClampedIntoTheValue) {
    // arrange a range that runs past the end, which an input method may name (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abc");
    Event commit{.kind = EventKind::TextInput, .text = "X", .replacement = TextInputRange{.location = 1, .length = 99}};
    // act
    field.dispatch_event(commit);
    // assert the whole tail went and nothing crashed, because the field clamps (§8.3)
    EXPECT_EQ(field.value(), "aX");
}

TEST(TextFieldImeChecks, ACommitWithNoRangeStillReplacesTheSelection) {
    // arrange a selected field, which is the ordinary insertion path (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcdef");
    field.set_selection(1, 3);
    Event commit{.kind = EventKind::TextInput, .text = "X"};
    // act
    field.dispatch_event(commit);
    // assert the selection went, because a commit with no range means the caret's own span (§8.3)
    EXPECT_EQ(field.value(), "aXdef");
}

TEST(TextFieldChecks, AFieldAttachedAfterTheTreeWasWiredStillFindsTheAnimationEngine) {
    // arrange a root that was handed the engine before the field existed, which is a dynamic attach (§7.3)
    EditingTextEngine text;
    AnimationEngine animations;
    UiNode root;
    root.set_animation_engine(&animations);
    auto field = std::make_unique<TextField>(nullptr, &text, "Albor");
    // act
    root.add_child(std::move(field));
    // assert the child learned the engine from the tree it joined, because a blink needs a clock (§7.3)
    ASSERT_EQ(root.child_count(), 1u);
    UiNode* joined = root.children().front().get();
    EXPECT_EQ(joined->animation_engine(), &animations);
}

TEST(TextFieldChecks, AFieldInsertedAfterTheTreeWasWiredStillFindsTheAnimationEngine) {
    // arrange a root with a child already in it, which insertion puts another beside (§8.1)
    EditingTextEngine text;
    AnimationEngine animations;
    UiNode root;
    root.set_animation_engine(&animations);
    root.add_child(std::make_unique<TextField>(nullptr, &text, "first"));
    auto field = std::make_unique<TextField>(nullptr, &text, "second");
    // act
    root.insert_child(1, std::move(field));
    // assert the inserted child learned it too, because insertion is attachment (§7.3)
    ASSERT_EQ(root.child_count(), 2u);
    EXPECT_EQ(root.children().at(1)->animation_engine(), &animations);
}

TEST(TextFieldChecks, AFieldAttachedToAWiredTreeBlinksWhenItTakesTheFocus) {
    // arrange the same late attachment, then a focus that needs the clock (§7.3)
    EditingTextEngine text;
    AnimationEngine animations;
    UiNode root;
    root.set_animation_engine(&animations);
    auto field = std::make_unique<TextField>(nullptr, &text, "Albor");
    field->set_caret(1);
    root.add_child(std::move(field));
    TextField* joined = dynamic_cast<TextField*>(root.children().front().get());
    ASSERT_NE(joined, nullptr);
    // act
    joined->set_focused(true);
    // assert a blink is running, because the engine reached the child through the tree (§7.3)
    EXPECT_EQ(animations.active_count(), 1u);
}

TEST(TextFieldImeChecks, AMarkOnAFieldWithNoEngineHoldsNoDisplayRun) {
    // arrange a field with no engine at all, which measures nothing it cannot (§9.1)
    TextField field(nullptr, nullptr);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    bool changed = false;
    field.set_on_change([&changed](const std::string&) { changed = true; });
    // act a composition arrives, which the field holds provisionally (§8.3)
    Event mark{.kind = EventKind::Composing, .text = "abc"};
    field.dispatch_event(mark);
    // assert the mark is held and no edit ran, because the provisional text is not the value (§8.3)
    EXPECT_TRUE(field.has_marked_text());
    EXPECT_EQ(field.marked_text(), "abc");
    EXPECT_FALSE(changed);
    EXPECT_TRUE(field.value().empty());
}

TEST(TextFieldScrollChecks, AFittingFieldDoesNotScroll) {
    // arrange a two line field given a plate tall enough to show both lines (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("ab\ncd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // assert content inside the plate offers no scroll, which is the zero range (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
    EXPECT_FLOAT_EQ(field.maximum_scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, AnOverflowingFieldReportsItsScrollRange) {
    // arrange a four line field given a plate of one line, which is what a fixed form field is (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // assert the range is the content height past the plate, which four lines over one is (§8.2)
    EXPECT_FLOAT_EQ(field.maximum_scroll_offset(), 44.0f);
}

TEST(TextFieldScrollChecks, MovingTheCaretPastTheBottomScrollsTheCaretIntoView) {
    // arrange a four line field in a one line plate, where the last line sits below the window (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act the caret moves onto the last line, which the plate could not show (§8.3)
    field.set_caret(7);
    // assert the viewport followed the caret, so its line is on screen (§8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
    // assert the caret line box lies inside the plate, which is what on screen means (§8.3)
    const Rect caret = field.caret_rect();
    EXPECT_GE(caret.y, 0.0f);
    EXPECT_LE(caret.y + caret.height, 20.0f + 0.001f);
}

TEST(TextFieldScrollChecks, MovingTheCaretBackToTheTopScrollsBack) {
    // arrange a field whose caret already scrolled the plate to the bottom (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(7);
    ASSERT_GT(field.scroll_offset(), 0.0f);
    // act the caret returns to the first line (§8.3)
    field.set_caret(0);
    // assert the plate scrolled back, because the first line is at the top of the content (§8.3)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, TheScrollOffsetClampsWhenTheContentShrinks) {
    // arrange a field scrolled to the bottom of a tall value (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.scroll_to(field.maximum_scroll_offset());
    ASSERT_FLOAT_EQ(field.scroll_offset(), 44.0f);
    // act the value shrinks to a run that fits the plate (§8.2)
    field.set_value("ab");
    // assert the offset came back inside the new range, which is empty (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, TheCaretRectReportsTheScrolledPosition) {
    // arrange a field whose last line the caret brought into view (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(7);
    // act
    const Rect caret = field.caret_rect();
    // assert the bar sits at the content top minus the scroll plus the line box, which is four here (§9.1)
    EXPECT_NEAR(caret.y, 4.0f, 0.001f);
    // assert the bar keeps the line height, because a caret is a line box bar (§9.1)
    EXPECT_NEAR(caret.height, 16.0f, 0.001f);
}

TEST(TextFieldScrollChecks, APressInScrolledContentResolvesTheRightByte) {
    // arrange a four line field scrolled to the bottom, where a low plate y covers the last line (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.scroll_to(field.maximum_scroll_offset());
    // act a press near the top of the plate lands over the last line of the scrolled content (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 4.0f}};
    field.dispatch_event(press);
    // assert the byte is the last line's first character, which the offset accounted for (§8.3)
    EXPECT_EQ(field.caret(), 6u);
}

TEST(TextFieldScrollChecks, ASelectionPaintsTranslatedByTheOffset) {
    // arrange a selected field scrolled so its early lines sit above the plate (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.select_all();
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.scroll_to(field.maximum_scroll_offset());
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 80.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert a highlight rect was carried above the plate, which only the scroll translation does (§6.1)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_FALSE(rects.empty());
    // the least y is the highest highlight, which a translated selection puts above the plate top (§6.1)
    float highest = rects.front().y;
    for (const Rect& rect : rects)
        highest = std::min(highest, rect.y);
    EXPECT_LT(highest, 0.0f);
}

TEST(TextFieldScrollChecks, ADragPastTheBottomScrollsWithoutChangingTheSelectionBytes) {
    // arrange a five line field in a one line plate, which the drag must scroll (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd\ne");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 4.0f}};
    field.dispatch_event(press);
    ASSERT_EQ(field.selection_start(), 0u);
    // act the drag runs past the plate's bottom edge, which is what reaching a hidden line means (§8.3)
    Event drag{.kind = EventKind::PointerMove, .position = Point{.x = 0.0f, .y = 60.0f}};
    field.dispatch_event(drag);
    // assert the content scrolled, so a line outside the window came into reach (§8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
    // assert the selection extends to the last line's first byte, which the scrolled press resolved (§8.3)
    EXPECT_EQ(field.selection_start(), 0u);
    EXPECT_EQ(field.selection_end(), 8u);
}

TEST(TextFieldScrollChecks, AReleaseEndsTheScrollAndASecondDragRestartsIt) {
    // arrange a field whose drag already scrolled the content away from the top (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd\ne");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 4.0f}};
    field.dispatch_event(press);
    Event drag{.kind = EventKind::PointerMove, .position = Point{.x = 0.0f, .y = 60.0f}};
    field.dispatch_event(drag);
    ASSERT_GT(field.scroll_offset(), 0.0f);
    Event release{.kind = EventKind::PointerUp, .position = Point{.x = 0.0f, .y = 60.0f}};
    field.dispatch_event(release);
    const float scrolled = field.scroll_offset();
    // act a move after the release drags nothing, so the offset holds where the release left it (§8.3)
    Event move_after{.kind = EventKind::PointerMove, .position = Point{.x = 0.0f, .y = 60.0f}};
    field.dispatch_event(move_after);
    // assert the released drag no longer scrolls, because the press that carried it ended (§8.3)
    EXPECT_FLOAT_EQ(field.scroll_offset(), scrolled);
}

TEST(TextFieldScrollChecks, ScrollToClampsIntoTheRange) {
    // arrange a four line field whose range is the content past the plate (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act a scroll above the leading edge, then one past the trailing edge (§8.2)
    field.scroll_to(-10.0f);
    // assert the offset stopped at the leading edge, because the content has no earlier place (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
    // act the far overshoot, which the range alone bounds (§8.2)
    field.scroll_to(9999.0f);
    // assert the offset stopped at the content end, which is the range the content defines (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 44.0f);
}

TEST(TextFieldScrollChecks, ASteadyScrollToTheSameOffsetRedrawsNothing) {
    // arrange a field already sitting at the leading edge (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.clear_dirty();
    // act a scroll that moves nowhere, because the offset already is zero (§8.2)
    field.scroll_to(0.0f);
    // assert the frame is unchanged, because a scroll with no movement changes no pixels (§6.1)
    EXPECT_FALSE(field.render_dirty());
}

TEST(TextFieldScrollChecks, ASingleLineFieldNeverScrolls) {
    // arrange a single line field with a long value in a plate that shows one line (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcdefghijklmnopqrstuvwxyz");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act the caret moves to the far end, which a single line still does without a scroll (§8.3)
    field.set_caret(26);
    // assert the offset stayed at the leading edge, because one line has nowhere to scroll (§8.2)
    EXPECT_FLOAT_EQ(field.maximum_scroll_offset(), 0.0f);
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
    // act a drag runs past the bottom edge, which must not scroll a single line field (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 10.0f}};
    field.dispatch_event(press);
    Event drag{.kind = EventKind::PointerMove, .position = Point{.x = 0.0f, .y = 100.0f}};
    field.dispatch_event(drag);
    // assert nothing scrolled, because only a multi line field has rows to move (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, AScrollingCaretMoveNeverTouchesTheValue) {
    // arrange a scrolled field holding a value and a caret the scroll will carry (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(7);
    const std::string before_value = field.value();
    const std::size_t before_caret = field.caret();
    // act the plate scrolls, which is a view change and not an edit (§8.2)
    field.scroll_to(0.0f);
    // assert neither the value nor the caret moved, because scrolling is visual only (§8.2)
    EXPECT_EQ(field.value(), before_value);
    EXPECT_EQ(field.caret(), before_caret);
    EXPECT_FALSE(field.has_selection());
}

TEST(TextFieldScrollChecks, TypingABreakAtTheBottomKeepsTheCaretLineInView) {
    // arrange a field scrolled so its last line fills the plate, with the caret at the end (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(5);
    ASSERT_GT(field.scroll_offset(), 0.0f);
    // act a break is typed at the end, which opens an empty line below (§8.3)
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    field.dispatch_event(event);
    // assert the empty line the caret moved onto stayed on screen, not scrolled back to the top (§8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
    const Rect caret = field.caret_rect();
    EXPECT_GE(caret.y, 0.0f);
    EXPECT_LE(caret.y + caret.height, 20.0f + 0.001f);
}

TEST(TextFieldScrollChecks, AProgrammaticScrollSurvivesARelayout) {
    // arrange a field scrolled to the bottom while its caret stays at the top (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.scroll_to(44.0f);
    ASSERT_FLOAT_EQ(field.scroll_offset(), 44.0f);
    // act the plate is laid out again, which must not snap the view back to the caret (§8.2)
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // assert the programmatic scroll is still where the caller put it (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 44.0f);
}

TEST(TextFieldScrollChecks, ARelayoutClampsAProgrammaticScrollIntoRange) {
    // arrange a field scrolled to the bottom of a tall value (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.scroll_to(44.0f);
    ASSERT_FLOAT_EQ(field.scroll_offset(), 44.0f);
    // act the plate grows tall enough to show the whole value (§8.2)
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 64.0f}));
    // assert the offset came back inside the now empty range (§8.2)
    EXPECT_FLOAT_EQ(field.maximum_scroll_offset(), 0.0f);
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, AnEditWrapsAtTheWidthThePlateLastMeasured) {
    // arrange a wide value wrapped by a layout pass, and a handler that samples the range inside an edit (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("abcdefghijklmnopqrstuvwxyz");
    Size measured{};
    field.measure(SizeConstraints::at_most(Size{80.0f, 400.0f}), measured);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 20.0f}));
    float range_in_edit = -1.0f;
    field.set_on_change([&field, &range_in_edit](const std::string&) { range_in_edit = field.maximum_scroll_offset(); });
    // act an edit lands on the wrapped value, which must keep wrapping like the plate (§8.2)
    Event event{.kind = EventKind::TextInput, .text = "z"};
    field.dispatch_event(event);
    // assert the range inside the edit is the wrapped one, not a single unbounded line (§8.2)
    EXPECT_GT(range_in_edit, 0.0f);
}

TEST(TextFieldScrollChecks, ACompositionInAScrolledFieldPaintsByTheCaretLine) {
    // arrange a width wrapped field whose caret sits on a line the plate cannot show (§8.3, §9.1)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("abcdefghij");
    Size measured{};
    field.measure(SizeConstraints::at_most(Size{55.0f, 400.0f}), measured);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{55.0f, 20.0f}));
    field.set_caret(9);
    ASSERT_GT(field.scroll_offset(), 0.0f);
    // act a composition opens at the caret, which the field paints under (§8.3)
    Event mark{.kind = EventKind::Composing, .text = "Z"};
    field.dispatch_event(mark);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    field.render(recorder);
    // assert the mark underline lands on the caret's line inside the plate, not above it (§8.2)
    float underline_y = -1.0f;
    for (const Rect& rect : rect_commands(*recorder.scene()))
        if (rect.height == 1.0f)
            underline_y = rect.y;
    EXPECT_GE(underline_y, 0.0f);
    EXPECT_LE(underline_y, 20.0f);
}

TEST(TextFieldScrollChecks, ASingleLineFieldCannotBeScrolledProgrammatically) {
    // arrange a single line field in a plate shorter than its line, which a parent may clamp to (§8.2)
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "abcdef");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 10.0f}));
    // assert a single line offers no range even when the plate is short (§8.2)
    EXPECT_FLOAT_EQ(field.maximum_scroll_offset(), 0.0f);
    // act a programmatic scroll asks for one, which must be refused (§8.2)
    field.scroll_to(8.0f);
    // assert nothing scrolled, because one line has nowhere to move (§8.2)
    EXPECT_FLOAT_EQ(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, TheTextOriginFollowsTheScroll) {
    // arrange a field whose caret brought the plate down to the last line (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(7);
    ASSERT_GT(field.scroll_offset(), 0.0f);
    // act
    const Point origin = field.text_origin();
    // assert the first byte is placed under the same scroll the paint uses (§8.2)
    EXPECT_FLOAT_EQ(origin.y, -field.scroll_offset());
}

TEST(TextFieldScrollChecks, AProgrammaticSelectionScrollsItsActiveEndIntoView) {
    // arrange a tall field whose caret starts at the top, with the whole value below the plate (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    ASSERT_FLOAT_EQ(field.scroll_offset(), 0.0f);
    // act the whole value is selected, which moves the active end to the last line (§8.3)
    field.select_all();
    // assert the active end was brought into view, so the selection is usable (§8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, SettingAValueFollowsTheCaretItKept) {
    // arrange a tall field whose caret sits on the last line while the view is at the top (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    field.set_caret(7);
    field.scroll_to(0.0f);
    ASSERT_FLOAT_EQ(field.scroll_offset(), 0.0f);
    // act a new value of the same shape is set, which keeps the caret byte (§8.3)
    field.set_value("w\nx\ny\nz");
    // assert the kept caret was brought back on screen (§8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
}

TEST(TextFieldScrollChecks, ADragPastTheEdgeKeepsTheDraggedEndInView) {
    // arrange a ten line field in a one line plate, whose whole value is not yet on screen (§8.3)
    EditingTextEngine engine;
    TextField field(nullptr, &engine);
    field.set_multiline(true);
    field.set_value("a\nb\nc\nd\ne\nf\ng\nh\ni\nj");
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{.x = 0.0f, .y = 4.0f}};
    field.dispatch_event(press);
    // act the pointer is dragged far below the plate in one move, so the view must follow it (§8.3)
    Event drag{.kind = EventKind::PointerMove, .position = Point{.x = 0.0f, .y = 1000.0f}};
    field.dispatch_event(drag);
    // assert the view followed the dragged end and stopped inside the content it can show (§8.2, §8.3)
    EXPECT_GT(field.scroll_offset(), 0.0f);
    EXPECT_LE(field.scroll_offset(), field.maximum_scroll_offset());
    const Rect caret = field.caret_rect();
    EXPECT_GE(caret.y, 0.0f);
    EXPECT_LE(caret.y + caret.height, 20.0f + 0.001f);
}
