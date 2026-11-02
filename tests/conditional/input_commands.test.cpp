// the typing command focuses a control and delivers text through the stage, which the automation host wires (§10.3)

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
#include "albor/layout/column_layout.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/text/text_engine.h"
#include "albor/widgets/label.h"
#include "albor/widgets/text_field.h"
#include "albor/window/platform_window_factory.h"

namespace
{
    // the engine double that measures to an empty layout, which keeps the typing checks off any shaper (§9.1)
    class TestTextEngine final : public albor::TextEngine
    {
    public:
        // override: hand out the empty layout, because the wiring under test never draws (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view, float) const override { return {}; }

        // override: hand out the empty layout at the explicit size, because the wiring under test never draws (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view, float, float) const override { return {}; }

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
