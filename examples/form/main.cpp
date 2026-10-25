#include <cstdio>
#include <vector>

#include "application/application.h"
#include "style/theme.h"
#include "todo/todo.h"
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
    // the application builds the compiled component over its engines and hands back the facade (§8.1, §11.3)
    albor::WidgetForm* form = nullptr;
    const albor::WindowHandle handle = application.create_window(options, form);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid() || form == nullptr) {
        std::fprintf(stderr, "form: the window could not be created\n");
        return 1;
    }
    // a window the registry dropped cannot be themed, which the failure states rather than crashes on (§5.2)
    albor::Window* window = application.window(handle);
    if (window == nullptr) {
        std::fprintf(stderr, "form: the window is not open\n");
        return 1;
    }
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    const albor::SystemAppearance appearance = window->system_appearance();
    // the controller builds the macos preset over the accents the system reports, which is the native control set (§8.4)
    albor::ThemeController controller(light_accents(appearance.accent), dark_accents(appearance.accent), albor::ThemePreset::Macos);
    // the controller owns the theme the whole tree borrows, so it is themed through the same presets (§6.1, §8.4)
    controller.install(*application.root(handle), appearance.scheme);
    // the scene is cleared with the themed surface, which is what the native faces read against (§6.1, §8.4)
    application.set_window_background(handle, controller.theme().surface());
    // populate sample todos for the form (demonstrates real data in a ListBox)
    if (form) {
        std::vector<albor::Todo> sample_todos;
        sample_todos.push_back({{"t01"}, {"Buy groceries"}});
        sample_todos.push_back({{"t02"}, {"Walk the dog"}});
        sample_todos.push_back({{"t03"}, {"Write ListBox UI demo"}});
        sample_todos.push_back({{"t04"}, {"Run tests"}});
        sample_todos.push_back({{"t05"}, {"Commit changes"}});
        sample_todos.push_back({{"t06"}, {"Review pull request"}});
        sample_todos.push_back({{"t07"}, {"Reply to email"}});
        sample_todos.push_back({{"t08"}, {"Water the plants"}});
        sample_todos.push_back({{"t09"}, {"Book dentist"}});
        sample_todos.push_back({{"t10"}, {"Pay the rent"}});
        sample_todos.push_back({{"t11"}, {"Call mum"}});
        sample_todos.push_back({{"t12"}, {"Read a book"}});
        sample_todos.push_back({{"t13"}, {"Plan the sprint"}});
        sample_todos.push_back({{"t14"}, {"Fix the bug"}});
        sample_todos.push_back({{"t15"}, {"Write the report"}});
        sample_todos.push_back({{"t16"}, {"Take a walk"}});
        form->set_todos(std::move(sample_todos));
    }
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
