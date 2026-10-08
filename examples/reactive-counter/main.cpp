#include <chrono>
#include <cstdio>
#include <memory>

#include "application/application.h"
#include "examples/reactive-counter/counter_panel.h"
#include "input/input_controller.h"
#include "platform/macos/event_loop.h"
#include "render/frame_scene.h"
#include "render/render-skia/skia_renderer_backend.h"
#include "render/render_executor.h"
#include "runtime/binding_engine.h"
#include "text/text-skia/skia_text_engine.h"
#include "ui/frame_pipeline.h"
#include "window/window.h"

int main() {
    // the example is the composition root, so it installs the macos platform, the skia backend and the text engine (§6.3, §9.1)
    albor::macos::install_macos_platform();
    albor::install_skia_renderer_backend();
    albor::SkiaTextEngine::install();
    // the renderer backend owns the metal device and the skia context (§6.3)
    std::unique_ptr<albor::RendererBackend> renderer = albor::create_renderer_backend();
    if (!renderer || !renderer->ready()) {
        std::fprintf(stderr, "reactive-counter: the skia metal backend did not come up\n");
        return 1;
    }
    // the text engine shapes the bound label, which the panel borrows from the application (§9.1)
    std::unique_ptr<albor::TextEngine> text_engine = albor::create_text_engine();
    if (!text_engine || !text_engine->ready()) {
        std::fprintf(stderr, "reactive-counter: the skia text engine did not come up\n");
        return 1;
    }
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Reactive Counter";
    options.size = albor::Size{800.0f, 600.0f};
    const albor::WindowHandle handle = application.create_window(options);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "reactive-counter: the window could not be created\n");
        return 1;
    }
    albor::Window* window = application.window(handle);
    // the surface wraps the metal layer of the window, which is the macos presentation path (§6.3, AD-16)
    std::unique_ptr<albor::RenderSurface> surface = renderer->create_surface(window->native_handle(), window->metrics());
    if (!surface) {
        std::fprintf(stderr, "reactive-counter: the render surface could not be created\n");
        return 1;
    }
    // the reactive engine owns the dependency graph that settles the bindings (§7.3)
    albor::BindingEngine engine;
    // the panel declares its reactive properties over that engine and measures its label through the text engine (§7.2, §9.1)
    auto panel = std::make_unique<albor::CounterPanel>(&engine, text_engine.get());
    // the pipeline owns the tree and drives stage one on the ui thread (§6.1, AD-18)
    albor::FramePipeline pipeline(engine, std::move(panel));
    pipeline.set_background(albor::Color::rgb(0.05f, 0.06f, 0.08f));
    // the input controller routes the native events into the panel, which is the input path of §8.3
    albor::InputController input;
    input.attach(pipeline.root());
    // stage two runs on its own thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
    albor::RenderExecutor executor(renderer.get());
    pipeline.set_executor(&executor);
    pipeline.set_surface(surface.get());
    // the ui thread builds a frame per pacing tick and hands it to the render thread (§6.1)
    application.set_frame_handler([&] {
        // the window may already be gone when a queued tick arrives (§4.2)
        if (application.window(handle) == nullptr)
            return;
        // the metrics are read on every tick, so a resize is answered by the very next frame (§9.1)
        const albor::WindowMetrics metrics = window->metrics();
        // stage one settles the graph, lays out the tree and records the immutable scene (§6.1, §7.3)
        const std::shared_ptr<const albor::FrameScene> scene = pipeline.build(metrics);
        // stage two executes it off the ui thread, which never runs application code (§6.1, AD-18)
        pipeline.submit(scene);
    });
    // a native input event reaches the example, which forwards it into the tree of the one window (§8.3)
    application.set_input_handler([&](albor::WindowHandle source, const albor::Event& event) {
        // only the example window is routed, so another window is left alone (§5.2)
        if (source != handle)
            return;
        // the controller routes the event and records the handled outcome, which the panel answers (§8.3)
        albor::Event routed = event;
        input.deliver(routed);
        // a key press that the panel handled is the reset gesture, which restores the count (§8.3)
        if (event.kind == albor::EventKind::KeyDown && routed.handled)
            static_cast<albor::CounterPanel&>(pipeline.root()).reset();
    });
    // a native resize remeasures the tree, which is what the layout invalidation is for (§8.2)
    application.set_resize_handler([&](albor::WindowHandle resized) {
        // only the example window is remeasured, so another window is left alone (§5.2)
        if (resized == handle)
            pipeline.root().invalidate_layout();
    });
    // a timer drives the count, which is what makes the scene change without any input (§4.2)
    const albor::TimerHandle ticker = application.timers().schedule_repeating(
        [&] {
            // the window may already be gone when a queued tick arrives (§4.2)
            if (application.window(handle) == nullptr)
                return;
            // the imperative write breaks nothing, because the count carries no binding (§7.3, AD-24)
            static_cast<albor::CounterPanel&>(pipeline.root()).increment();
        },
        std::chrono::milliseconds(500));
    // the timer handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "reactive-counter: the ticker was not registered\n");
    // a closing window terminates the application, which is the ordinary shutdown path (§4.1)
    application.set_close_handler([&application](albor::WindowHandle) {
        application.quit(0);
        return true;
    });
    std::printf("reactive-counter: running, the count advances every 500ms, click to increment, any key to reset, close the window to quit\n");
    // the loop blocks until the window closes, which asks the application to quit (§4.1)
    return application.run();
}
