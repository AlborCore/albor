#include <cstdio>
#include <memory>

#include "application/application.h"
#include "widget_form.h"

namespace
{
    // the tree the form window shows, which is the compiled component over the engines the stage owns (§8.1, §11.3)
    std::unique_ptr<albor::UiNode> build_scene(albor::BindingEngine& engine, albor::TextEngine& text) {
        // the generated facade is the whole tree, which needs the text engine for its labels (§9.1, §11.3)
        return std::make_unique<albor::WidgetForm>(&engine, &text);
    }
} // namespace

int main() {
    // the platform and the backends installed themselves at load, so the application runs on them (§5.3, §9.1)
    albor::Application& application = albor::Application::instance();
    // the window is a main window, whose tree the application builds from the compiled component (§6.1, §8.3)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Widget Form";
    options.size = albor::Size{420.0f, 560.0f};
    const albor::WindowHandle handle = application.create_window(options, build_scene);
    // a window that refused its tree leaves nothing to show, which the invalid handle reports (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "form: the window could not be created\n");
        return 1;
    }
    // the loop blocks until the last window closes, which ends the application (§4.1)
    return application.run();
}
