#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/application/application.h"
#include "albor/automation/automation_protocol.h"
#include "albor/automation/automation_session.h"
#include "albor/automation/host/input_commands.h"
#include "albor/automation/semantic_commands.h"
#include "albor/commands/command_registry.h"
#include "albor/commands/shortcut.h"
#include "albor/core/json.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/text/text_engine.h"
#include "albor/ui/semantic_tree_builder.h"
#include "albor/ui/ui_node.h"
#include "albor/window/platform_window_factory.h"
#include "todo/todo.h"
#include "widget_form.h"

namespace
{
    // the engine double that measures every byte at a fixed advance, which gives the sample controls a real area (§9.1)
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
} // namespace

TEST(ReferenceApplication, TheAutomationSurfaceDrivesTheSampleForm) {
    // arrange the headless platform, the sample form and the automation registry (§10.3, §16)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WidgetForm* form = nullptr;
    albor::RootBuilder builder = [&form](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the generated sample form is the tree a real application uses (§16)
        auto widget = std::make_unique<albor::WidgetForm>(&engine, &text);
        // the test keeps the facade, because it sets the sample data the example sets (§11.3)
        form = widget.get();
        // the form is the window tree (§8.1)
        return widget;
    };
    albor::WindowOptions options;
    options.title = "Reference";
    options.size = albor::Size{420.0f, 840.0f};
    const albor::WindowHandle handle = application.create_window(options, builder);
    ASSERT_TRUE(handle.valid());
    ASSERT_NE(form, nullptr);
    // the sample todos are the rows the form list repeats, which the example also sets (§16)
    form->set_todos(std::vector<albor::Todo>{albor::Todo{.key = "t01", .title = "Buy groceries"}, albor::Todo{.key = "t02", .title = "Walk the dog"}});
    // the frame arranges the generated tree, which is what the bounds query reads (§8.2)
    application.pump();
    // the semantic snapshot is rebuilt from the live tree for every command (§10.3)
    albor::SemanticTree semantic_tree;
    albor::automation::AutomationRegistry registry;
    albor::automation::register_semantic_commands(registry, [&application, handle, &semantic_tree] {
        // the tree is rebuilt from the sample, because a command may follow an edit or a click (§10.3)
        semantic_tree = albor::build_semantic_tree(*application.root(handle));
        // the rebuilt tree is what the command reads for this line (§10.3)
        return &semantic_tree;
    });
    // the input commands reach the live sample tree, which is where a click or a key lands (§8.3)
    albor::automation::register_input_commands(registry, [&application, handle] { return application.root(handle); });
    // every standard command is permitted, because this is the reference flow, not the allow-list test (§10.3, AD-10)
    for (const std::string& name : albor::automation::semantic_command_names())
        registry.allow(name);
    // the input family is permitted the same way, which is what lets the flow click and type (§10.3, AD-10)
    for (const std::string& name : albor::automation::input_command_names())
        registry.allow(name);
    // the session is built, which mints the token this test presents (§10.3, AD-10)
    albor::automation::AutomationSession session(registry);
    (void)session.handle(albor::automation::encode_handshake(session.token()));

    // act and assert the sample tree is reachable by its author-assigned identity (§18.1, AD-11)
    const albor::automation::AutomationResult found = session.handle("semantic.find form_press");
    ASSERT_TRUE(found.ok);
    EXPECT_EQ(found.payload, "button press me");

    // act and assert the bounds query answers the rect the layout arranged for the title (§8.2)
    const albor::automation::AutomationResult title_rect = session.handle("semantic.bounds form_title");
    ASSERT_TRUE(title_rect.ok);
    // the payload is json, because a client reads the four fields as one rect (§10.3)
    const std::optional<albor::core::Json> title = albor::core::Json::parse(title_rect.payload);
    ASSERT_TRUE(title.has_value());
    // the title carries the arranged origin, which is the leading edge the column assigns (§8.2)
    const double title_x = title->member("x")->as_number();
    const double title_y = title->member("y")->as_number();
    // the canvas is the sample's fixed extent, which a text engine cannot change (§8.2)
    const albor::automation::AutomationResult meter_rect = session.handle("semantic.bounds form_meter");
    ASSERT_TRUE(meter_rect.ok);
    // the canvas rect is parsed the same way, because the client reads geometry through the command (§10.3)
    const std::optional<albor::core::Json> meter = albor::core::Json::parse(meter_rect.payload);
    ASSERT_TRUE(meter.has_value());
    // the canvas fills the column content, which is the 420 window less the 16 padding on each side (§8.2)
    EXPECT_DOUBLE_EQ(meter->member("width")->as_number(), 388.0);
    EXPECT_DOUBLE_EQ(meter->member("height")->as_number(), 20.0);
    // the button sits below the title and shares the column's leading edge, which is the adjacency a test asserts (§8.2)
    const albor::automation::AutomationResult button_rect = session.handle("semantic.bounds form_press");
    ASSERT_TRUE(button_rect.ok);
    // the button rect is parsed the same way, because the client reads geometry through the command (§10.3)
    const std::optional<albor::core::Json> button = albor::core::Json::parse(button_rect.payload);
    ASSERT_TRUE(button.has_value());
    // the button is below the title, which is the vertical order the column authored (§8.2)
    EXPECT_GT(button->member("y")->as_number(), title_y);
    // the button shares the title's leading edge, because both are column children at the same padding (§8.2)
    EXPECT_DOUBLE_EQ(button->member("x")->as_number(), title_x);

    // act and assert the typed reads answer the sample's number, boolean and text values (§10.1)
    const albor::automation::AutomationResult level = session.handle("semantic.value form_level");
    ASSERT_TRUE(level.ok);
    // the slider value is a number, which is what a chart or form test reads without the dump (§10.3)
    EXPECT_EQ(level.kind, albor::automation::ValueKind::Number);
    EXPECT_DOUBLE_EQ(std::stod(level.payload), 0.5);
    // the stepper value is the second number the sample carries (§10.1)
    const albor::automation::AutomationResult effort = session.handle("semantic.value form_effort");
    ASSERT_TRUE(effort.ok);
    // the stepper is a number too, which proves the range path is not slider specific (§10.1)
    EXPECT_EQ(effort.kind, albor::automation::ValueKind::Number);
    EXPECT_DOUBLE_EQ(std::stod(effort.payload), 30.0);
    // the checkbox is the sample's boolean, which starts unchecked (§10.1)
    const albor::automation::AutomationResult armed = session.handle("semantic.value form_armed");
    ASSERT_TRUE(armed.ok);
    // the check is a boolean, which a form test asserts without parsing a dump (§10.3)
    EXPECT_EQ(armed.kind, albor::automation::ValueKind::Boolean);
    EXPECT_EQ(armed.payload, "no");
    // the field is the sample's text value, which starts empty (§10.1)
    const albor::automation::AutomationResult name = session.handle("semantic.value form_name");
    ASSERT_TRUE(name.ok);
    // the field is text even when empty, because its role is the value's shape (§10.1)
    EXPECT_EQ(name.kind, albor::automation::ValueKind::Text);
    EXPECT_EQ(name.payload, "");

    // act the client types into the sample field through the real edit path (§8.3)
    ASSERT_TRUE(session.handle("input.type form_name Ada").ok);
    // assert the typed read now answers the text the field holds (§10.1)
    const albor::automation::AutomationResult typed = session.handle("semantic.value form_name");
    ASSERT_TRUE(typed.ok);
    // the text is the value the client just typed, not a dump suffix (§10.3)
    EXPECT_EQ(typed.kind, albor::automation::ValueKind::Text);
    EXPECT_EQ(typed.payload, "Ada");

    // act the client clicks the sample button at its centre (§8.3)
    ASSERT_TRUE(session.handle("input.click form_press").ok);
    // the frame settles the binding the button's action dirtied, which is what updates the label (§7.3)
    application.pump();
    // assert the button's action ran, which is what the sample's label reports (§8.3)
    const albor::automation::AutomationResult pressed = session.handle("semantic.find form_presses");
    ASSERT_TRUE(pressed.ok);
    // the label carries the count the button's binding wrote (§10.1)
    EXPECT_EQ(pressed.payload, "label presses 1");

    // act the focused button receives return, which is the keyboard activation path (§8.3)
    ASSERT_TRUE(session.handle("input.key return").ok);
    // the frame settles the binding the key dirtied, which is what updates the label (§7.3)
    application.pump();
    // assert the key route reached the button and ran its action again (§8.3)
    const albor::automation::AutomationResult keyed = session.handle("semantic.find form_presses");
    ASSERT_TRUE(keyed.ok);
    // the count moved by one, because return is the second route to the same action (§18.5)
    EXPECT_EQ(keyed.payload, "label presses 2");

    // act the client reads the slider rect and drags it to three quarters across (§8.3)
    const albor::automation::AutomationResult slider_rect = session.handle("semantic.bounds form_level");
    ASSERT_TRUE(slider_rect.ok);
    // the rect is parsed, because the drag computes its points from the layout (§8.2)
    const std::optional<albor::core::Json> slider = albor::core::Json::parse(slider_rect.payload);
    ASSERT_TRUE(slider.has_value());
    // the drag starts a quarter across and ends three quarters across, which moves the value right (§8.3)
    const double slider_x = slider->member("x")->as_number();
    const double slider_width = slider->member("width")->as_number();
    const double press_x = slider_x + slider_width * 0.25;
    const double move_x = slider_x + slider_width * 0.75;
    // the vertical centre is the track the slider answers on (§8.3)
    const double track_y = slider->member("y")->as_number() + slider->member("height")->as_number() * 0.5;
    // act the press, move and release are three pointer commands, which is the real drag path (§8.3)
    ASSERT_TRUE(session.handle("input.pointer form_level press " + std::to_string(press_x) + " " + std::to_string(track_y)).ok);
    // the move continues the drag, which the slider's capture routes to its own handler (§8.3)
    ASSERT_TRUE(session.handle("input.pointer form_level move " + std::to_string(move_x) + " " + std::to_string(track_y)).ok);
    // the release ends the drag, which leaves the value where the move put it (§8.3)
    ASSERT_TRUE(session.handle("input.pointer form_level release " + std::to_string(move_x) + " " + std::to_string(track_y)).ok);
    // assert the typed read sees the steered value, which is the point of the drag (§10.1)
    const albor::automation::AutomationResult dragged = session.handle("semantic.value form_level");
    ASSERT_TRUE(dragged.ok);
    // the value moved right of the old midpoint, because the pointer ended at three quarters (§8.3)
    EXPECT_GT(std::stod(dragged.payload), 0.5);

    // arrange a sample command bound to control s, which is the accelerator path a client drives (§18.5)
    std::unique_ptr<albor::Command> shortcut = application.commands().declare("reference.shortcut");
    // the action counts the invocation, which is what the key press must produce (§18.3)
    std::size_t shortcut_calls = 0;
    shortcut->set_action([&shortcut_calls](const albor::CommandInvocation&) { shortcut_calls++; });
    // the shortcut is bound by the author id, which is the registry's only key (§18.5, AD-11)
    ASSERT_TRUE(application.commands().bind_shortcut(albor::Shortcut{.key = albor::Key::S, .modifiers = albor::Modifiers{.control = true}}, "reference.shortcut"));
    // act the client presses control s through the normalized key command (§10.3)
    ASSERT_TRUE(session.handle("input.key s control").ok);
    // assert the accelerator ran, which proves the key path reaches the application's commands (§18.5)
    EXPECT_EQ(shortcut_calls, 1u);

    // act the client clicks the sample checkbox, which is the boolean the form publishes (§8.3)
    ASSERT_TRUE(session.handle("input.click form_armed").ok);
    // assert the click toggled the state, which the typed read reports as a boolean (§10.1)
    const albor::automation::AutomationResult armed_after = session.handle("semantic.value form_armed");
    ASSERT_TRUE(armed_after.ok);
    // the state is now the true half of the boolean (§10.1)
    EXPECT_EQ(armed_after.kind, albor::automation::ValueKind::Boolean);
    EXPECT_EQ(armed_after.payload, "yes");
    // act the client clicks the checkbox again, which restores the sample's starting state (§8.3)
    ASSERT_TRUE(session.handle("input.click form_armed").ok);
    // assert the second click flipped the boolean back, because the checkbox stayed enabled (§10.1)
    const albor::automation::AutomationResult armed_again = session.handle("semantic.value form_armed");
    ASSERT_TRUE(armed_again.ok);
    // the sample is back to the state the flow started in (§8.3)
    EXPECT_EQ(armed_again.payload, "no");

    // cleanup the window, which owns the sample tree and the command the test declared (§8.1)
    EXPECT_TRUE(application.destroy_window(handle));
}
