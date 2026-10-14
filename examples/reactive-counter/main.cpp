#include <chrono>
#include <cstdio>

#include "application/application.h"
#include "examples/reactive-counter/counter_panel.h"

int main() {
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Reactive Counter";
    options.size = albor::Size{800.0f, 600.0f};
    // the application builds the component over the engines it owns and hands back the facade to drive (§8.1, §7.3)
    albor::CounterPanel* panel = nullptr;
    const albor::WindowHandle handle = application.create_window(options, panel);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid() || panel == nullptr) {
        std::fprintf(stderr, "reactive-counter: the window could not be created\n");
        return 1;
    }
    // a key the tree answered is the reset gesture, which the routed event reports (§8.3, AD-24)
    application.set_input_handler([handle, panel](albor::WindowHandle source, const albor::Event& event) {
        if (source != handle)
            return;
        if (event.kind == albor::EventKind::KeyDown && event.handled)
            panel->reset();
    });
    // a timer drives the count, which is what makes the scene change without any input (§4.2)
    const albor::TimerHandle ticker = application.timers().schedule_repeating([panel] { panel->increment(); }, std::chrono::milliseconds(500));
    // the handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "reactive-counter: the ticker was not registered\n");
    std::printf("reactive-counter: running, the count advances every 500ms, click to increment, any key to reset\n");
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
