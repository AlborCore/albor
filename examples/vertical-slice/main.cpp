#include <cstdio>

#include "application/application.h"
#include "commands/command.h"
#include "style/theme.h"
#include "vertical_slice.h"
#include "widgets/theme_controller.h"
#include "window/system_appearance.h"

namespace
{
    // the light accents, which are the window surface the system shows in light mode (§8.4)
    albor::Accents light_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}; }

    // the dark accents, which are the window surface the system shows in dark mode (§8.4)
    albor::Accents dark_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}; }
} // namespace

int main() {
    // the application owns the native loop and the window registry (§4.1)
    albor::Application& application = albor::Application::instance();
    // the slice opens at a size that shows both pages without scrolling the shell (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Vertical Slice";
    options.size = albor::Size{560.0f, 760.0f};
    // the compiled root constructs the imported form and table pages (§8.1, §11.3)
    albor::VerticalSlice* slice = nullptr;
    const albor::WindowHandle handle = application.create_window(options, slice);
    // a window that refused the tree leaves nothing to show (§5.1)
    if (!handle.valid() || slice == nullptr) {
        std::fprintf(stderr, "vertical-slice: the window could not be created\n");
        return 1;
    }
    // the window is needed for the close action and the system appearance (§5.2, §8.4)
    albor::Window* window = application.window(handle);
    // a window the registry dropped cannot be themed, which the failure states rather than crashes on (§5.2)
    if (window == nullptr) {
        std::fprintf(stderr, "vertical-slice: the window is not open\n");
        return 1;
    }
    // the command declaration in the ui owns the action, so the host installs its work once (§18.1, §18.4)
    albor::Command* apply = slice->command_apply();
    // the declared command joins the application registry, which is what automation and a menu both address (§18.1, AD-11)
    application.commands().join(*apply);
    apply->set_action([slice](const albor::CommandInvocation&) {
        // the revision is the observable effect of one apply, which the bound label shows (§18.2)
        slice->set_revision(slice->revision() + 1);
    });
    apply->set_undo_entry(albor::UndoEntry{
        .label = "Apply",
        .undo = [slice](const albor::CommandInvocation&) { slice->set_revision(slice->revision() - 1); },
        .redo = [slice](const albor::CommandInvocation&) { slice->set_revision(slice->revision() + 1); },
    });
    albor::Command* close = slice->command_close();
    // the close command joins too, because a menu item and an automation client share the same selector (§18.1)
    application.commands().join(*close);
    close->set_action([window](const albor::CommandInvocation&) {
        // the close command rides the same window path as the title bar button (§5.1)
        window->close();
    });
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    const albor::SystemAppearance appearance = window->system_appearance();
    // the controller builds the macos preset over the reported accents, which is the native control set (§8.4)
    albor::ThemeController controller(light_accents(appearance.accent), dark_accents(appearance.accent), albor::ThemePreset::Macos);
    // the tree carries the theme from its root down, which every widget resolves through (§6.1)
    controller.install(*application.root(handle), appearance.scheme);
    // the scene is cleared with the themed surface, which is what the native faces read against (§6.1, §8.4)
    application.set_window_background(handle, controller.theme().surface());
    std::printf("vertical-slice: form and table pages are running\n");
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
