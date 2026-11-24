// the input commands type, click, press and key the tree through the stage, which the automation host wires (§10.3)

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/automation/automation_protocol.h"
#include "albor/automation/automation_session.h"
#include "albor/automation/host/input_commands.h"
#include "albor/commands/command_registry.h"
#include "albor/commands/shortcut.h"
#include "albor/focus/focus_manager.h"
#include "albor/layout/column_layout.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/text/text_engine.h"
#include "albor/ui/ui_event.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/button.h"
#include "albor/widgets/checkbox.h"
#include "albor/widgets/label.h"
#include "albor/widgets/scroll_view.h"
#include "albor/widgets/slider.h"
#include "albor/widgets/text_field.h"
#include "albor/window/platform_window_factory.h"

namespace
{
    // the engine double that measures every byte at a fixed advance, which gives the controls a real area (§9.1)
    class TestTextEngine final : public albor::TextEngine
    {
    public:
        // the advance one byte of text contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height any run reports, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // override: measure at the default size, which the explicit overload implements (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        // override: shape one glyph per byte, which is enough to give a control an area (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // an empty run carries no glyphs, so it measures as nothing (§9.1)
            if (text.empty())
                return albor::TextLayout{};
            // the double shapes one line, which is what a label and a button read (§9.1)
            albor::ShapedRun::Line line;
            // the line origin is the baseline the synthesized glyphs hang from (§9.1)
            line.origin = albor::Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            // the line covers the whole double text, which is what a decoration range indexes (§9.1)
            line.start_byte = 0;
            // the end is the byte count, because the whole run is one line (§9.1)
            line.end_byte = text.size();
            // the line box is the geometry the selection and caret read (§9.1)
            line.ascent = HEIGHT * 0.8f;
            // the descent is the other half of the line box (§9.1)
            line.descent = HEIGHT * 0.2f;
            // every byte becomes one glyph placed a fixed advance apart (§9.1)
            for (const char character : text)
                line.glyphs.push_back(albor::ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = albor::Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(line.glyphs.size())});
            // the advance counts bytes, and the ceiling clamps the result the way a container expects (§8.2)
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            // the line width is what a caret past the last glyph resolves against (§9.1)
            line.width = advanced;
            // the shaped run travels beside the extents, which is what a measurement hands over (§9.1)
            return albor::TextLayout{.run = albor::ShapedRun::make({std::move(line)}, font_size), .size = albor::Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        // override: report the engine usable, because the application checks readiness (§9.1)
        [[nodiscard]] bool ready() const override { return true; }
    };

    // the scope that installs the headless platform, the cpu backend and the engine double, and clears after (§5.3, AD-16)
    class InstalledScope
    {
    public:
        InstalledScope() {
            albor::set_text_engine_factory([] { return std::make_unique<TestTextEngine>(); });
            albor::software::install_software_renderer_backend();
            albor::headless::install_headless_platform();
        }

        ~InstalledScope() {
            albor::set_text_engine_factory(nullptr);
            albor::set_renderer_backend_factory(nullptr);
            albor::set_platform_window_factory(nullptr);
            albor::set_platform_event_loop_factory(nullptr);
        }

        InstalledScope(const InstalledScope&) = delete;
        InstalledScope& operator=(const InstalledScope&) = delete;
    };

    // the node double that records the key pair, which proves both halves and the modifiers arrive (§8.3)
    class KeyRecordingNode final : public albor::UiNode
    {
    public:
        // how many presses arrived, which is the first half of the pair (§8.3)
        std::size_t downs = 0;

        // how many releases arrived, which is the second half of the pair (§8.3)
        std::size_t ups = 0;

        // the modifiers the last press carried, which the plus separated list fills (§18.5)
        albor::Modifiers modifiers{};

        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override {
            // the node fills the offer, because the key tests do not exercise layout (§8.2)
            out_size = constraints.constrain(albor::Size{10.0f, 10.0f});
            m_measured_size = out_size;
        }

        void dispatch_event(albor::Event& event) override {
            // a key press is counted with the modifiers it carried (§8.3)
            if (event.kind == albor::EventKind::KeyDown) {
                downs++;
                modifiers = event.modifiers;
            }
            // a key release is counted as the second half of the pair (§8.3)
            if (event.kind == albor::EventKind::KeyUp)
                ups++;
            // the node claims the key, because it is the focused editor (§8.3)
            event.handled = true;
        }
    };

    // the content double that reports a tall extent, which gives a viewport a range to scroll (§8.2)
    class TallContent final : public albor::UiNode
    {
    public:
        void measure(const albor::SizeConstraints&, albor::Size& out_size) override {
            // the extent is taller than the viewport, which is what makes the wheel move it (§8.2)
            out_size = albor::Size{200.0f, 600.0f};
            m_measured_size = out_size;
        }
    };

    // the node double that records the wheel it receives, which proves the delta, unit and point survive (§8.3)
    class WheelRecordingNode final : public albor::UiNode
    {
    public:
        // how many wheel events arrived, which the command's single event sets (§10.3)
        std::size_t wheels = 0;

        // the delta the last wheel carried, which is the signed distance (§8.3)
        float scroll = 0.0f;

        // the unit the last wheel carried, which the precision name selects (§8.3)
        albor::ScrollPrecision precision = albor::ScrollPrecision::Line;

        // the point the last wheel carried, which the optional pair replaces (§8.3)
        albor::Point point{};

        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override {
            // the node fills the offer, so the wheel command finds its centre (§8.2)
            out_size = constraints.constrain(albor::Size{200.0f, 100.0f});
            m_measured_size = out_size;
        }

        void dispatch_event(albor::Event& event) override {
            // only the wheel is recorded, because the command delivers one (§8.3)
            if (event.kind != albor::EventKind::Wheel)
                return;
            wheels++;
            scroll = event.scroll;
            precision = event.precision;
            point = event.position;
            event.handled = true;
        }
    };
} // namespace

TEST(InputCommands, ATypingCommandFocusesTheFieldAndTypesIntoIt) {
    // arrange a form whose only control is a multi line field (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::TextField* notes = nullptr;
    albor::RootBuilder form = [&notes](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the field, which is the control a client types into (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field edits more than one line, which is what makes the break a character (§8)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is the only selector the typing command offers (AD-11)
        field->set_explicit_id("notes");
        // the field edits a document, so a typed break stays (§8.3)
        field->set_multiline(true);
        // the test keeps the control, because it reads the text back after typing (§8.3)
        notes = field.get();
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the field, which gives focus a rect to land in (§8.2, §8.3)
    application.pump();
    // arrange the typing command over the live tree, permitted the way a host permits it (§10.3, AD-10)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client types a sentence with a space and a line break (§10.3, §8.3)
    const albor::automation::AutomationResult result = session.handle("input.type notes " + albor::automation::encode_argument("first line\nsecond line"));
    // assert the field holds the typed text, which is the real edit path (§8.3)
    ASSERT_TRUE(result.ok);
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->value(), "first line\nsecond line");
    // assert the command moved the focus to the field before typing (§8.3)
    EXPECT_EQ(application.stage(handle)->focus().focused(), notes);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingCommandSplitsUtf8CodePoints) {
    // arrange a form with one multi line field (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::TextField* notes = nullptr;
    albor::RootBuilder form = [&notes](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the field the client types into (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field is multi line, which keeps the value exactly as typed (§8)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is the only selector (AD-11)
        field->set_explicit_id("notes");
        // a document keeps every code point (§8.3)
        field->set_multiline(true);
        // the test reads the value back through the control (§8.3)
        notes = field.get();
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act a two byte and a four byte sequence, which the splitter must keep whole (§9.1)
    const albor::automation::AutomationResult result = session.handle("input.type notes " + albor::automation::encode_argument("\xC3\xA9\xF0\x9F\x98\x80"));
    // assert the field holds the two code points byte for byte (§9.1)
    ASSERT_TRUE(result.ok);
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->value(), "\xC3\xA9\xF0\x9F\x98\x80");
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingCommandRefusesAnUnknownId) {
    // arrange a form and a permitted typing command (§10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns one identifiable field, which the command can find (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field is the only control, so an unknown id names nothing (§8.3)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is what a client selects by (AD-11)
        field->set_explicit_id("notes");
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act
    const albor::automation::AutomationResult result = session.handle("input.type missing hello");
    // assert the refusal names the selector, because guessing a node would type into the wrong field (§10.3, AD-11)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no node with id: missing"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingCommandRefusesANodeThatEditsNoText) {
    // arrange a form whose only control is a label, which takes no text (§10.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns one identifiable label (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the label carries text but edits none (§10.1)
        auto label = std::make_unique<albor::Label>(&engine, &text, "caption");
        // the identity is what a client selects by (AD-11)
        label->set_explicit_id("caption");
        // the label joins the column (§8.1)
        column->add_child(std::move(label));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act
    const albor::automation::AutomationResult result = session.handle("input.type caption hello");
    // assert the command refuses rather than answering success for a node that swallowed the text (§10.1)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("edits no text"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingCommandRefusesAReadOnlyField) {
    // arrange a form whose only field is read only (§10.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the read only field (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field refuses edits while keeping its text selectable (§10.1)
        auto field = std::make_unique<albor::TextField>(&engine, &text, "fixed");
        // the identity is what a client selects by (AD-11)
        field->set_explicit_id("notes");
        // the field refuses edits (§10.1)
        field->set_read_only(true);
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act
    const albor::automation::AutomationResult result = session.handle("input.type notes hello");
    // assert the refusal says why, because a client must not believe it typed (§10.1, §10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("read only"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingCommandRefusesAValueThatWasNotEscaped) {
    // arrange a form with one field and a permitted typing command (§10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the field, which is the target of the refused call (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field edits one line (§8.3)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is what a client selects by (AD-11)
        field->set_explicit_id("notes");
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act a text with a space that the client did not escape (§10.3)
    const albor::automation::AutomationResult result = session.handle("input.type notes hello world");
    // assert the command refuses rather than typing one truncated word (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("takes an id and text"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, ATypingChangeThatClosesTheWindowDoesNotFreeTheStage) {
    // arrange a form whose change handler closes the window, which the dispatch must survive (§4.2)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowHandle handle;
    albor::RootBuilder form = [&application, &handle](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the field the client types into (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field reports every edit, which is where the close is requested (§8)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is the only selector (AD-11)
        field->set_explicit_id("notes");
        // the change closes the window, which must not free the stage under the dispatch (§4.2)
        field->set_on_change([&application, &handle](const std::string&) { (void)application.request_close(handle); });
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Form";
    handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    // arrange the typing command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.type");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act typing the first code point fires the change that closes the window (§8.3)
    const albor::automation::AutomationResult result = session.handle("input.type notes abc");
    // assert the command survived, because the teardown is posted rather than run on the dispatch stack (§4.2)
    EXPECT_TRUE(result.ok);
    EXPECT_NE(application.window(handle), nullptr);
    application.pump();
    // assert the posted teardown ran on the next pass (§4.2)
    EXPECT_EQ(application.window(handle), nullptr);
}

TEST(InputCommands, AClickPressesTheButtonAtItsCentre) {
    // arrange a form whose only control is a button, which is what a pointer presses (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    std::size_t presses = 0;
    albor::RootBuilder form = [&presses](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the button, which the command finds by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the button is the control a pointer activates (§8.3)
        auto button = std::make_unique<albor::Button>(&engine, &text, "Press");
        // the identity is the only selector the click command offers (AD-11)
        button->set_explicit_id("press");
        // the press is counted, which proves the real pointer path ran (§8.3)
        button->set_on_click([&presses] { presses++; });
        // the button joins the column (§8.1)
        column->add_child(std::move(button));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the button, which gives the centre a real point (§8.2)
    application.pump();
    // arrange the click command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.click");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client clicks the button at its centre (§10.3)
    const albor::automation::AutomationResult result = session.handle("input.click press");
    // assert the command succeeded and the button ran its action (§8.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(presses, 1u);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AClickTogglesACheckbox) {
    // arrange a form whose only control is a checkbox, which a pointer flips (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::CheckBox* box = nullptr;
    albor::RootBuilder form = [&box](albor::BindingEngine& engine, albor::TextEngine&) {
        // the column owns the box, which the command finds by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the box is the control a press toggles (§8.3)
        auto control = std::make_unique<albor::CheckBox>(&engine);
        // the identity is the only selector the click command offers (AD-11)
        control->set_explicit_id("armed");
        // the test reads the state the press moved (§8.3)
        box = control.get();
        // the box joins the column (§8.1)
        column->add_child(std::move(control));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the box, which gives the centre a real point (§8.2)
    application.pump();
    // arrange the click command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.click");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client clicks the box (§10.3)
    const albor::automation::AutomationResult result = session.handle("input.click armed");
    // assert the press reached the box and flipped its state (§8.3)
    ASSERT_TRUE(result.ok);
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->checked());
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, APointerSequenceDragsASlider) {
    // arrange a form whose only control is a slider, which a drag steers (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::Slider* slider = nullptr;
    albor::RootBuilder form = [&slider](albor::BindingEngine& engine, albor::TextEngine&) {
        // the column owns the slider, which the command finds by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the slider starts halfway, so both directions are available (§8)
        auto control = std::make_unique<albor::Slider>(&engine);
        // the identity is the only selector the pointer command offers (AD-11)
        control->set_explicit_id("level");
        // the range is what the drag maps a point onto (§8)
        control->set_range(0.0f, 1.0f);
        // the starting value is the middle of the range (§8)
        control->set_value(0.5f);
        // the test reads the value the drag produced (§8)
        slider = control.get();
        // the slider joins the column (§8.1)
        column->add_child(std::move(control));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the slider, which gives the drag real coordinates (§8.2)
    application.pump();
    ASSERT_NE(slider, nullptr);
    // the area is the rect the layout assigned, which a client reads through the bounds command (§8.2)
    const albor::Rect area = slider->bounds();
    // the press is a quarter across, which is left of the current value (§8.3)
    const float press_x = area.x + area.width * 0.25f;
    // the move is three quarters across, which is the steered end of the drag (§8.3)
    const float move_x = area.x + area.width * 0.75f;
    // the vertical centre is the track the slider answers on (§8.3)
    const float y = area.y + area.height * 0.5f;
    // arrange the pointer command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.pointer");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client presses, moves and releases, which is a drag (§8.3)
    const albor::automation::AutomationResult press = session.handle("input.pointer level press " + std::to_string(press_x) + " " + std::to_string(y));
    const albor::automation::AutomationResult move = session.handle("input.pointer level move " + std::to_string(move_x) + " " + std::to_string(y));
    const albor::automation::AutomationResult release = session.handle("input.pointer level release " + std::to_string(move_x) + " " + std::to_string(y));
    // assert every step of the gesture was delivered (§10.3)
    ASSERT_TRUE(press.ok);
    ASSERT_TRUE(move.ok);
    ASSERT_TRUE(release.ok);
    // assert the drag moved the value to the right, which is what the pointer path means (§8.3)
    EXPECT_GT(slider->value(), 0.6f);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AKeyPressActivatesTheFocusedButton) {
    // arrange a focused button, which is what a key press reaches (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    std::size_t presses = 0;
    albor::RootBuilder form = [&presses](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the button, which the commands find by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the button is the control a key activates (§8.3)
        auto button = std::make_unique<albor::Button>(&engine, &text, "Press");
        // the identity is the only selector the commands offer (AD-11)
        button->set_explicit_id("press");
        // every activation is counted, whatever route produced it (§8.3)
        button->set_on_click([&presses] { presses++; });
        // the button joins the column (§8.1)
        column->add_child(std::move(button));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the button, which gives the click a real point (§8.2)
    application.pump();
    // arrange the click and key commands over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.click");
    registry.allow("input.key");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the click focuses the button, and return activates it (§8.3)
    const albor::automation::AutomationResult click = session.handle("input.click press");
    const albor::automation::AutomationResult key = session.handle("input.key return");
    // assert both routes reached the button, because the pointer focused it and the key activated it (§8.3)
    ASSERT_TRUE(click.ok);
    ASSERT_TRUE(key.ok);
    EXPECT_EQ(presses, 2u);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AKeyPressDrivesAnAccelerator) {
    // arrange a window with a command bound to control s, which is the shortcut path (§18.5)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame wires the window, which is what the key path reaches (§6.1)
    application.pump();
    // the command is declared on the application registry, which the stage borrows (§18.1)
    std::unique_ptr<albor::Command> command = application.commands().declare("test.shortcut");
    // the action counts the invocation, which is what the key press must produce (§18.3)
    std::size_t invocations = 0;
    command->set_action([&invocations](const albor::CommandInvocation&) { invocations++; });
    // the shortcut is bound by the author id, which is the only selector the registry indexes (§18.5, AD-11)
    ASSERT_TRUE(application.commands().bind_shortcut(albor::Shortcut{.key = albor::Key::S, .modifiers = albor::Modifiers{.control = true}}, "test.shortcut"));
    // arrange the key command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.key");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client presses control s, which is the accelerator (§18.5)
    const albor::automation::AutomationResult result = session.handle("input.key s control");
    // assert the normalized key route found the command and invoked it (§18.5)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(invocations, 1u);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AKeyWithNoNameIsRefused) {
    // arrange a window, whose active tree names the key window (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); };
    albor::WindowOptions options;
    options.title = "Input";
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame wires the window, which is what the key path reaches (§6.1)
    application.pump();
    // arrange the key command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.key");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client names a key the vocabulary does not carry (§10.3)
    const albor::automation::AutomationResult result = session.handle("input.key nope");
    // assert the refusal names the key, because guessing one would press the wrong control (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown key: nope"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AKeyPressCarriesThePlusJoinedModifiers) {
    // arrange a focused node that records the key pair, which is what the command must deliver (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    KeyRecordingNode* node = nullptr;
    albor::RootBuilder form = [&node](albor::BindingEngine&, albor::TextEngine&) {
        // the node is the whole tree, so the window arranges it to the content area (§8.1)
        auto root = std::make_unique<KeyRecordingNode>();
        // the test reads the events the node recorded (§8.3)
        node = root.get();
        // the node is the window tree (§8.1)
        return root;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the node, which gives focus a rect to land in (§8.2)
    application.pump();
    ASSERT_NE(node, nullptr);
    // the caret is placed on the node, which is where the key path routes (§8.3)
    application.stage(handle)->focus().request_focus(node, albor::FocusReason::Keyboard);
    // arrange the key command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.key");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client presses a with control and shift, which is the plus separated list (§18.5)
    const albor::automation::AutomationResult result = session.handle("input.key a control+shift");
    // assert the press arrived, which is the half the input controller delivers to the tree (§8.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(node->downs, 1u);
    // assert the release stayed out of the tree, because the controller ignores a key up (§8.3)
    EXPECT_EQ(node->ups, 0u);
    // assert both modifiers arrived, because the plus list is the whole reason for the separator (§18.5)
    EXPECT_TRUE(node->modifiers.control);
    EXPECT_TRUE(node->modifiers.shift);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, APointerCancelEndsTheDrag) {
    // arrange a slider that starts in a drag, which is what a cancel must end (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::Slider* slider = nullptr;
    albor::RootBuilder form = [&slider](albor::BindingEngine& engine, albor::TextEngine&) {
        // the column owns the slider, which the command finds by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the slider starts halfway, so the drag has a visible direction (§8)
        auto control = std::make_unique<albor::Slider>(&engine);
        // the identity is the only selector the pointer command offers (AD-11)
        control->set_explicit_id("level");
        // the range is what the drag maps a point onto (§8)
        control->set_range(0.0f, 1.0f);
        // the starting value is the middle of the range (§8)
        control->set_value(0.5f);
        // the test reads the value the cancel left behind (§8)
        slider = control.get();
        // the slider joins the column (§8.1)
        column->add_child(std::move(control));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the slider, which gives the drag real coordinates (§8.2)
    application.pump();
    ASSERT_NE(slider, nullptr);
    // the area is the rect the layout assigned, which the drag points come from (§8.2)
    const albor::Rect area = slider->bounds();
    // the press is a quarter across, which is left of the current value (§8.3)
    const float press_x = area.x + area.width * 0.25f;
    // the cancel names the point the gesture reached, which the event model supports (§8.3)
    const float cancel_x = area.x + area.width * 0.75f;
    // the move after the cancel is the point that must not steer the value (§8.3)
    const float move_x = area.x + area.width * 0.9f;
    // the vertical centre is the track the slider answers on (§8.3)
    const float y = area.y + area.height * 0.5f;
    // arrange the pointer command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.pointer");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the press starts the drag, the cancel ends it, and the move finds no capture (§8.3)
    ASSERT_TRUE(session.handle("input.pointer level press " + std::to_string(press_x) + " " + std::to_string(y)).ok);
    ASSERT_TRUE(session.handle("input.pointer level cancel " + std::to_string(cancel_x) + " " + std::to_string(y)).ok);
    ASSERT_TRUE(session.handle("input.pointer level move " + std::to_string(move_x) + " " + std::to_string(y)).ok);
    // assert the value stayed where the press put it, because the cancel ended the drag (§8.3)
    EXPECT_LT(slider->value(), 0.5f);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AClickRefusesAControlWithNoHitPoint) {
    // arrange a disabled button, which refuses every pointer probe (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder form = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns the disabled button, which the click finds by identity (§8.1)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the button is the control that refuses the click (§8.3)
        auto button = std::make_unique<albor::Button>(&engine, &text, "Press");
        // the identity is the only selector the click command offers (AD-11)
        button->set_explicit_id("press");
        // the disabled state makes every hit test false (§8.3)
        button->set_disabled(true);
        // the button joins the column (§8.1)
        column->add_child(std::move(button));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    // the frame arranges the button, which gives its bounds a real area (§8.2)
    application.pump();
    // arrange the click command over the live tree (§10.3)
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.click");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act the client clicks the disabled button (§10.3)
    const albor::automation::AutomationResult result = session.handle("input.click press");
    // assert the refusal names the node, because a click that cannot land must not answer success (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no clickable point: press"), std::string::npos);
    // cleanup
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AWheelScrollsTheViewportUnderTheNode) {
    // arrange a window whose only control scrolls a tall content (§8.3, §10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::ScrollView* viewport = nullptr;
    albor::RootBuilder form = [&viewport](albor::BindingEngine& engine, albor::TextEngine&) {
        auto column = std::make_unique<albor::ColumnLayout>();
        auto view = std::make_unique<albor::ScrollView>(&engine);
        view->set_explicit_id("viewport");
        view->set_viewport_extent(albor::Size{200.0f, 100.0f});
        view->set_scrollbar_visibility(albor::ScrollbarVisibility::AlwaysOff);
        view->add_child(std::make_unique<TallContent>());
        viewport = view.get();
        column->add_child(std::move(view));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    ASSERT_NE(viewport, nullptr);
    ASSERT_GT(viewport->maximum_offset(), 0.0f);
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.wheel");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act a down notch, which moves the content toward the end (§8.3)
    const albor::automation::AutomationResult wheel = session.handle("input.wheel viewport -1");
    // assert the command was delivered and the viewport followed it by one themed line (§10.3, §8.3)
    ASSERT_TRUE(wheel.ok);
    EXPECT_GT(viewport->offset(), 0.0f);
    EXPECT_TRUE(viewport->user_scrolled());
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AWheelTowardTheStartReducesTheOffset) {
    // arrange a scrolled viewport, which the command must move back toward the start (§8.3, §10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::ScrollView* viewport = nullptr;
    albor::RootBuilder form = [&viewport](albor::BindingEngine& engine, albor::TextEngine&) {
        auto column = std::make_unique<albor::ColumnLayout>();
        auto view = std::make_unique<albor::ScrollView>(&engine);
        view->set_explicit_id("viewport");
        view->set_viewport_extent(albor::Size{200.0f, 100.0f});
        view->set_scrollbar_visibility(albor::ScrollbarVisibility::AlwaysOff);
        view->add_child(std::make_unique<TallContent>());
        viewport = view.get();
        column->add_child(std::move(view));
        return column;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    ASSERT_NE(viewport, nullptr);
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.wheel");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    ASSERT_TRUE(session.handle("input.wheel viewport -1").ok);
    ASSERT_GT(viewport->offset(), 0.0f);
    const float advanced = viewport->offset();
    // act a positive notch, which is the platform's direction toward the start (§8.3)
    const albor::automation::AutomationResult wheel = session.handle("input.wheel viewport 1");
    // assert the offset moved back by the same line and stayed a user scroll (§8.3, §10.3)
    ASSERT_TRUE(wheel.ok);
    EXPECT_LT(viewport->offset(), advanced);
    EXPECT_TRUE(viewport->user_scrolled());
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, TheWheelCommandIsAdvertisedByTheCommandNames) {
    // act the host's allow list helper is asked for its names (§10.3, AD-10)
    const auto names = albor::automation::input_command_names();
    // assert the wheel is one of them, because a direct allow in a test would hide its removal (§10.3)
    EXPECT_TRUE(std::find(names.begin(), names.end(), "input.wheel") != names.end());
}

TEST(InputCommands, AWheelCarriesItsPrecisionAndExplicitPoint) {
    // arrange a control that records the wheel it receives, which is where the fields land (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    WheelRecordingNode* recorder = nullptr;
    albor::RootBuilder form = [&recorder](albor::BindingEngine&, albor::TextEngine&) -> std::unique_ptr<albor::UiNode> {
        auto node = std::make_unique<WheelRecordingNode>();
        node->set_explicit_id("wheel");
        recorder = node.get();
        return node;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    ASSERT_NE(recorder, nullptr);
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.wheel");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act a point precision wheel over an explicit spot (§10.3)
    const albor::automation::AutomationResult wheel = session.handle("input.wheel wheel -40 point 30 60");
    // assert the delta, unit and point reached the control unchanged (§8.3)
    ASSERT_TRUE(wheel.ok);
    ASSERT_EQ(recorder->wheels, 1U);
    EXPECT_FLOAT_EQ(recorder->scroll, -40.0f);
    EXPECT_EQ(recorder->precision, albor::ScrollPrecision::Point);
    EXPECT_EQ(recorder->point, (albor::Point{30.0f, 60.0f}));
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(InputCommands, AWheelRefusesMalformedCalls) {
    // arrange a control the wheel can reach, which keeps each refusal about the arguments (§10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    WheelRecordingNode* recorder = nullptr;
    albor::RootBuilder form = [&recorder](albor::BindingEngine&, albor::TextEngine&) -> std::unique_ptr<albor::UiNode> {
        auto node = std::make_unique<WheelRecordingNode>();
        node->set_explicit_id("wheel");
        recorder = node.get();
        return node;
    };
    albor::WindowOptions options;
    options.title = "Input";
    options.size = albor::Size{200.0f, 100.0f};
    const albor::WindowHandle handle = application.create_window(options, form);
    ASSERT_TRUE(handle.valid());
    application.pump();
    ASSERT_NE(recorder, nullptr);
    albor::automation::AutomationRegistry registry;
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    registry.allow("input.wheel");
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));
    // act every malformed shape a client might send (§10.3)
    EXPECT_FALSE(session.handle("input.wheel wheel").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel nope").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel nan").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel inf").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel -1 sideways").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel -1 line 30").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel -1 line 1 2 3").ok);
    EXPECT_FALSE(session.handle("input.wheel wheel -1 point nope 4").ok);
    EXPECT_FALSE(session.handle("input.wheel missing -1").ok);
    // assert no event reached the control, because a refused call must not scroll (§10.3)
    EXPECT_EQ(recorder->wheels, 0U);
    EXPECT_TRUE(application.destroy_window(handle));
}
