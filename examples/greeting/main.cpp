#include <chrono>
#include <cstdio>

#include "application/application.h"
#include "drivability.h"
#include "greeting_panel.h"
#include "style/theme.h"
#include "widgets/theme_controller.h"
#include "window/window.h"

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
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Greeting";
    options.size = albor::Size{800.0f, 600.0f};
    // the application builds the compiled component over its engines and hands back the facade (§8.1, §11.3)
    albor::GreetingPanel* panel = nullptr;
    const albor::WindowHandle handle = application.create_window(options, panel);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid() || panel == nullptr) {
        std::fprintf(stderr, "greeting: the window could not be created\n");
        return 1;
    }
    // a window the registry dropped cannot be themed, which the failure states rather than crashes on (§5.2)
    albor::Window* window = application.window(handle);
    if (window == nullptr) {
        std::fprintf(stderr, "greeting: the window is not open\n");
        return 1;
    }
    // the generated signal slot is wired here, which is how a host reacts to the button (§8.3)
    panel->set_on_run_clicked([] { std::printf("greeting: the run_clicked signal fired\n"); });
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    const albor::SystemAppearance appearance = window->system_appearance();
    // the controller builds the macos preset over the accents the system reports, which is the native control set (§8.4)
    albor::ThemeController controller(light_accents(appearance.accent), dark_accents(appearance.accent), albor::ThemePreset::Macos);
    // the controller owns the theme the whole tree borrows, so it is themed through the same presets (§6.1, §8.4)
    controller.install(*application.root(handle), appearance.scheme);
    // the scene is cleared with the themed surface, which is what the native faces read against (§6.1, §8.4)
    application.set_window_background(handle, controller.theme().surface());
    // the drivability is built over the live tree, which is what assistive technology and automation read (§10.1, §10.3)
    albor::example::Drivable drivable(*application.root(handle), window->native_handle().object());
    // the semantic tree is rebuilt after the bindings settled, which is what that frame slot is for (§10.1)
    if (!application.set_frame_hook(handle, [&drivable] { drivable.refresh(); }))
        std::fprintf(stderr, "greeting: the window took no frame hook\n");
    // a timer advances the count, which the bound labels announce without any input (§4.2, §7.3)
    const albor::TimerHandle ticker = application.timers().schedule_repeating([panel] { panel->set_click_count(panel->click_count() + 1); }, std::chrono::milliseconds(500));
    // the handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "greeting: the ticker was not registered\n");
    std::printf("greeting: running, the count advances every 500ms, close the window to quit\n");
    std::printf("greeting: GreetingPanel generated from greeting_panel.ui, click Increment to raise the signal\n");
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
