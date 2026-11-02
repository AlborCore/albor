#include <cstdio>

// the working tree exports the albor/ folder, and only the pinned entry names a release that predates it (AD-19)
#if defined(ALBOR_CONSUMER_RELEASED_INCLUDES)
#include "application/application.h"
#else
#include "albor/application/application.h"
#endif
#include "consumer_panel.h"

int main() {
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();

    albor::WindowOptions options;
    options.title = "albor consumer fixture";
    options.size = albor::Size{320.0f, 240.0f};

    // the generated facade arrives from the installed codegen driver, which the helpers locate (§11.3, AD-19)
    albor::ConsumerPanel* panel = nullptr;
    const albor::WindowHandle handle = application.create_window(options, panel);
    if (!handle.valid() || panel == nullptr) {
        std::fprintf(stderr, "albor-consumer: the window could not be created\n");
        return 1;
    }

    // the framework's recommended runtime answered the link, which is what the package verifies (§3.3, AD-19)
    return application.run();
}
