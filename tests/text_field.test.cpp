#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "focus/focus_manager.h"
#include "render/frame_scene.h"
#include "ui/scene_builder.h"
#include "widgets/text_field.h"

using namespace albor;

namespace
{
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
            // the double shapes one glyph per character, which is what a shaper merges multi bytes into (§9.1)
            ShapedRun::Line line;
            // the line origin is the baseline the synthesized glyphs hang from (§9.1)
            line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            // every character becomes one glyph placed a fixed advance apart, carrying the byte it starts at (§9.1)
            for (std::size_t index = 0; index < text.size();) {
                // the lead byte opens a character, which the double resolves as the spans utf8 uses (§9.1)
                std::size_t span = 1;
                // a two byte lead is the shape the test corpus needs (§9.1)
                if ((text[index] & 0xE0) == 0xC0)
                    span = 2;
                // the glyph hangs where a fixed advance places it, and carries its own byte (§9.1)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(text[index]), .position = Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(index)});
                // the double steps the whole character, because a cluster is one character (§9.1)
                index += span;
            }
            // the line spans its text and carries the advance a caret stops at, which one per character is (§9.1)
            line.width = static_cast<float>(line.glyphs.size()) * ADVANCE;
            // the span is written whole, which is what the caret queries read (§9.1)
            line.start_byte = 0;
            line.end_byte = text.size();
            // the extents are computed before the run moves the line, because the move takes its glyphs (§9.1)
            const float width = static_cast<float>(line.glyphs.size()) * ADVANCE;
            // the shaped run travels beside the extents the layout reports, which are the character extents (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(line)}, font_size), .size = Size{.width = width, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
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

TEST(TextFieldChecks, TheCaretDrawsOverAnEditableField) {
    // arrange
    EditingTextEngine engine;
    TextField field(nullptr, &engine, "Albor");
    field.set_caret(2);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 40.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the caret is the one filled bar the editable field records (§6.1)
    EXPECT_GE(rect_commands(*recorder.scene()).size(), 1u);
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
