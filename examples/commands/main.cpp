// the shared action object of AD-15, demonstrated across a toolbar, a menu style row and the keyboard (§18)

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "application/application.h"
#include "commands/command_registry.h"
#include "layout/column_layout.h"
#include "layout/row_layout.h"
#include "style/theme.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/theme_controller.h"
#include "widgets/widget.h"
#include "window/system_appearance.h"

namespace
{
    // the example title, which names what the window demonstrates (§5.1)
    constexpr const char* TITLE = "Albor \xE2\x80\x94 Commands";

    // the route an invocation arrived by, which the example prints into the label both consumers read (§18.3)
    std::string route_name(albor::InvocationSource source) {
        switch (source) {
        case albor::InvocationSource::Pointer:
            return "pointer";
        case albor::InvocationSource::Keyboard:
            return "keyboard";
        case albor::InvocationSource::Menu:
            return "menu";
        case albor::InvocationSource::Automation:
            return "automation";
        case albor::InvocationSource::Accessibility:
            return "accessibility";
        case albor::InvocationSource::UndoRedo:
            return "undo";
        case albor::InvocationSource::Handler:
            return "handler";
        }
        return "unknown";
    }

    // the light accents, which are the window surface the system shows in light mode with the reported accent (§8.4)
    albor::Accents light_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}; }

    // the dark accents, which are the window surface the system shows in dark mode with the reported accent (§8.4)
    albor::Accents dark_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}; }

    // build the scene, which declares the commands once and binds three consumers to each (§18.1, AD-15)
    std::unique_ptr<albor::UiNode> build_scene(albor::BindingEngine& engine, albor::TextEngine& text) {
        // the registry is the application's own, so every window and every control reads one object (§18.1)
        albor::CommandRegistry& commands = albor::Application::instance().commands();
        // the commands are held past this call, because the tree that binds to them outlives it (§18.1)
        static std::vector<std::unique_ptr<albor::Command>> held;
        // the save action, which three consumers below all bind to rather than each owning a copy (§18.1)
        held.push_back(commands.declare("file.save", "Save"));
        albor::Command& save = *held.back();
        save.set_action([&save](const albor::CommandInvocation& invocation) {
            // the label is a published field, so writing it here repaints both bound buttons at once (§18.2)
            save.set_label(std::string("Save — last from the ") + route_name(invocation.source) + " route");
        });
        // the reversal is declared once, so a pointer and a key contribute one entry rather than two (§18.4)
        save.set_undo_entry({
            .label = "Save",
            .undo = [](const albor::CommandInvocation&) {},
            .redo = [](const albor::CommandInvocation&) {},
        });

        // the delete action, which is a second shared object and demonstrates the selection scope (§18.3)
        held.push_back(commands.declare("row.erase", "Delete Rows"));
        albor::Command& erase = *held.back();
        erase.set_action([&erase](const albor::CommandInvocation& invocation) {
            // a selection scoped action reports the range it ran over, which is what an action over rows needs (§18.3)
            erase.set_label(std::string("Delete Rows — ran over ") + std::to_string(invocation.selection.count()) + " rows");
        });
        // the action needs the current selection, so an empty one refuses rather than running over nothing (§18.3)
        erase.set_selection_required(true);
        erase.set_undo_entry({
            .label = "Delete Rows",
            .undo = [](const albor::CommandInvocation&) {},
            .redo = [](const albor::CommandInvocation&) {},
        });

        // the selection is three rows wide, which the source reports on demand rather than caching (§18.3)
        commands.set_selection_source([] { return albor::SelectionSpan{.first = 2, .last = 5}; });

        // the accelerators route into the same objects, which is what a keyboard equivalent is (§18.5)
        commands.shortcuts().bind(albor::Shortcut{.key = albor::Key::S, .modifiers = albor::Modifiers{.command = true}}, save.id());
        commands.shortcuts().bind(albor::Shortcut{.key = albor::Key::E, .modifiers = albor::Modifiers{.command = true}}, erase.id());
        // the undo command publishes the stack's own answer as its enablement, so it needs no polling (§18.4)
        commands.shortcuts().bind(albor::Shortcut{.key = albor::Key::Z, .modifiers = albor::Modifiers{.command = true}}, commands.undo_command().command().id());

        // the scene is a column, because the demonstration reads top to bottom (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        column->set_gap(12.0f);
        column->set_padding(16.0f);

        // the first consumer is a toolbar style row, which is what a screen shows at the top (§18.1)
        auto toolbar = std::make_unique<albor::RowLayout>();
        toolbar->set_gap(8.0f);
        // the save button takes its label and its enablement from the shared object (§18.7)
        auto save_button = std::make_unique<albor::Button>(&engine, &text, std::string{}, "toolbar_save");
        save_button->set_command(&save);
        toolbar->add_child(std::move(save_button));
        // the undo button is a command like any other, whose enablement is the history's answer (§18.4)
        auto undo_button = std::make_unique<albor::Button>(&engine, &text, std::string{}, "toolbar_undo");
        undo_button->set_command(&commands.undo_command().command());
        toolbar->add_child(std::move(undo_button));
        // the redo button completes the pair, and both follow the same stack (§18.4)
        auto redo_button = std::make_unique<albor::Button>(&engine, &text, std::string{}, "toolbar_redo");
        redo_button->set_command(&commands.redo_command().command());
        toolbar->add_child(std::move(redo_button));
        column->add_child(std::move(toolbar));

        // the second consumer is a menu style row, which binds the very same object and holds no state of its own (§18.1)
        auto menu = std::make_unique<albor::RowLayout>();
        menu->set_gap(8.0f);
        // this save button is a second consumer, not a second command, which is the whole point of the module (§18.1)
        auto menu_save = std::make_unique<albor::Button>(&engine, &text, std::string{}, "menu_save");
        menu_save->set_command(&save);
        menu->add_child(std::move(menu_save));
        // the delete action is bound here too, so the selection scope is visible from a control (§18.3)
        auto menu_erase = std::make_unique<albor::Button>(&engine, &text, std::string{}, "menu_erase");
        menu_erase->set_command(&erase);
        menu->add_child(std::move(menu_erase));
        column->add_child(std::move(menu));

        // the third consumer is the keyboard, which the window routes through the registry the application owns (§18.5)
        auto keys = std::make_unique<albor::Label>(&engine, &text, "Cmd+S saves, Cmd+E deletes, Cmd+Z undoes — all three share one object", "keys");
        column->add_child(std::move(keys));

        // withholding permission on one object is what greys out every consumer at once (§18.2)
        auto armed = std::make_unique<albor::CheckBox>(&engine, "armed");
        armed->set_on_toggle([&save](bool checked) {
            // one write reaches the toolbar, the menu row and the accelerator, because they read one object (§18.7)
            save.set_enabled(!checked);
        });
        // the box pairs with its own label, which is how a native check box reads (§8.4)
        auto armed_row = std::make_unique<albor::RowLayout>();
        armed_row->set_gap(8.0f);
        armed_row->add_child(std::move(armed));
        armed_row->add_child(std::make_unique<albor::Label>(&engine, &text, "withhold saves", "armed_label"));
        column->add_child(std::move(armed_row));

        // the tree returns the column, which the stage owns from here (§8.1)
        return column;
    }
} // namespace

int main() {
    // the platform and the backends installed themselves at load, so the application runs on them (§5.3, §9.1)
    albor::Application& application = albor::Application::instance();
    // the window is a main window, whose tree the application builds through the scene above (§6.1, §8.3)
    albor::WindowOptions options;
    options.title = TITLE;
    options.size = albor::Size{560.0f, 260.0f};
    const albor::WindowHandle handle = application.create_window(options, build_scene);
    // a window that refused its tree leaves nothing to show, which the invalid handle reports (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "commands: the window could not be created\n");
        return 1;
    }
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    albor::Window* window = application.window(handle);
    // a window the registry dropped cannot be themed, which the failure states rather than crashes on (§5.2)
    if (window == nullptr) {
        std::fprintf(stderr, "commands: the window is not open\n");
        return 1;
    }
    const albor::SystemAppearance appearance = window->system_appearance();
    // the macos preset over the reported accents is the native control set, which the tree borrows (§8.4, §6.1)
    albor::ThemeController controller(light_accents(appearance.accent), dark_accents(appearance.accent), albor::ThemePreset::Macos);
    // the tree carries the theme from its root down, which is what every widget resolves through (§6.1)
    controller.install(*application.root(handle), appearance.scheme);
    // the scene is cleared with the themed surface, which is what the native faces read against (§6.1, §8.4)
    application.set_window_background(handle, controller.theme().surface());
    // a system appearance change rebuilds and reinstalls the theme, so the window follows the settings live (§8.4)
    application.set_appearance_handler([&](albor::WindowHandle changed, const albor::SystemAppearance& changed_appearance) {
        // only this example window is answered, so another window is left alone (§5.2)
        if (changed != handle || application.window(handle) == nullptr)
            return;
        // the accents are rebuilt from the reported accent, and the scheme follows the report (§8.4)
        controller.set_accents(light_accents(changed_appearance.accent), dark_accents(changed_appearance.accent));
        controller.set_scheme(changed_appearance.scheme);
        // the frame background follows the surface the theme resolved (§8.4)
        application.set_window_background(handle, controller.theme().surface());
    });
    std::printf("commands: running, one action bound to a toolbar, a menu row and the keyboard\n");
    // the loop blocks until the last window closes, which ends the application (§4.1)
    return application.run();
}
