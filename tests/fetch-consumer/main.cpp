#include <cstdio>

#include "albor/application/application.h"
#include "fetch_panel.h"

int main() {
    // the application owns the native loop, the window registry and the task dispatcher (§5.1)
    albor::Application& application = albor::Application::instance();
    // the options name a role-free main window for this consumer (§5.2)
    albor::WindowOptions options;
    // the title identifies the fixture in a window list
    options.title = "albor fetch consumer fixture";
    // the size keeps the generated layout inside a small window
    options.size = albor::Size{320.0f, 240.0f};
    // the application fills the generated facade when it creates the window (§11)
    albor::FetchPanel* panel = nullptr;
    // the window creation wires the component to the framework engines (§5.1)
    const albor::WindowHandle handle = application.create_window(options, panel);
    // a handle without a facade means the fetched targets did not register
    if (!handle.valid() || panel == nullptr) {
        // the diagnostic names the fixture so a CI log points at the failing step
        std::fprintf(stderr, "albor-fetch-consumer: the window could not be created\n");
        // the failure ends the process before the event loop starts
        return 1;
    }
    // the run returns when the last main window closes, which ends the smoke process (§5.1)
    return application.run();
}
