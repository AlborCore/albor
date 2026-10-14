#include <cstdio>

#include "application/application.h"
#include "drivability.h"
#include "style/theme.h"
#include "widget_form.h"
#include "widgets/theme_controller.h"
#include "window/system_appearance.h"

namespace
{
    // the light accents, which are the window surface the system shows in light mode with the reported accent (§8.4)
    albor::Accents light_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}; }

    // the dark accents, which are the window surface the system shows in dark mode with the reported accent (§8.4)
    albor::Accents dark_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}; }

} // namespace

int main() {
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Widget Form";
    options.size = albor::Size{420.0f, 560.0f};
    // the application builds the compiled component over the engines it owns, and linking chose the backends (§8.1, §6.3)
    albor::WidgetForm* form = nullptr;
    const albor::WindowHandle handle = application.create_window(options, form);
    // a window that refused its tree leaves nothing to show, which the invalid handle reports (§5.1)
    if (!handle.valid() || form == nullptr) {
        std::fprintf(stderr, "form: the window could not be created\n");
        return 1;
    }
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    albor::Window* window = application.window(handle);
    // a window the registry dropped cannot be themed, which the failure states rather than crashes on (§5.2)
    if (window == nullptr) {
        std::fprintf(stderr, "form: the window is not open\n");
        return 1;
    }
    const albor::SystemAppearance appearance = window->system_appearance();
    // the controller builds the macos preset over the accents the system reports, which is the native control set (§8.4)
    albor::ThemeController controller(light_accents(appearance.accent), dark_accents(appearance.accent), albor::ThemePreset::Macos);
    // the controller owns the theme the whole tree borrows, so it outlives every widget of the window (§6.1)
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
    // the drivability publishes the semantic tree and serves automation clients (§10.1, §10.3)
    albor::example::Drivable drivable(*application.root(handle), window->native_handle().object());
    // the automation channel is served on every pump, because a waiting client must not wait a second (§10.3)
    application.set_frame_handler([&] {
        // the window may already be gone when a queued tick arrives (§4.2)
        if (application.window(handle) == nullptr)
            return;
        // the semantic tree is rebuilt from the live tree, then one client is served (§10.1, §10.3)
        drivable.refresh();
    });
    std::printf("form: automation at %s\n", drivable.endpoint().c_str());
    // the loop blocks until the last window closes, which ends the application (§4.1)
    return application.run();
}
