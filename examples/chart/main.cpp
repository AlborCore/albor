#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "application/application.h"
#include "examples/chart/waveform_widget.h"
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
        std::fprintf(stderr, "chart: the skia metal backend did not come up\n");
        return 1;
    }
    // the text engine shapes the label runs, which the widgets borrow from the application (§9.1)
    std::unique_ptr<albor::TextEngine> text_engine = albor::create_text_engine();
    if (!text_engine || !text_engine->ready()) {
        std::fprintf(stderr, "chart: the skia text engine did not come up\n");
        return 1;
    }
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Chart";
    options.size = albor::Size{800.0f, 600.0f};
    const albor::WindowHandle handle = application.create_window(options);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "chart: the window could not be created\n");
        return 1;
    }
    albor::Window* window = application.window(handle);
    // the surface wraps the metal layer of the window, which is the macos presentation path (§6.3, AD-16)
    std::unique_ptr<albor::RenderSurface> surface = renderer->create_surface(window->native_handle(), window->metrics());
    if (!surface) {
        std::fprintf(stderr, "chart: the render surface could not be created\n");
        return 1;
    }
    // the reactive engine owns the dependency graph that settles the bindings (§7.3)
    albor::BindingEngine engine;
    // the manifest glue generated the facade, which the hand written widget implements (§11.2, AD-17)
    auto widget = std::make_unique<albor::WaveformWidget>(&engine);
    // the generated signal slot is wired here, which is how a host reacts to a selection (§11.2)
    widget->set_on_selection_changed([](float time_pos) { std::printf("chart: selection at %.1fs\n", time_pos); });
    // the pipeline owns the tree and drives stage one on the ui thread (§6.1, AD-18)
    albor::FramePipeline pipeline(engine, std::move(widget));
    pipeline.set_background(albor::Color::rgb(0.05f, 0.06f, 0.08f));
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
    // a native resize remeasures the tree, which is what the layout invalidation is for (§8.2)
    application.set_resize_handler([&](albor::WindowHandle resized) {
        // only the example window is remeasured, so another window is left alone (§5.2)
        if (resized == handle)
            pipeline.root().invalidate_layout();
    });
    // a timer pushes a new snapshot and raises the selection signal, which the generated facade publishes (§4.2, §11.2)
    int tick = 0;
    const albor::TimerHandle ticker = application.timers().schedule_repeating(
        [&] {
            // the window may already be gone when a queued tick arrives (§4.2)
            if (application.window(handle) == nullptr)
                return;
            albor::WaveformWidget& root = static_cast<albor::WaveformWidget&>(pipeline.root());
            // the samples describe one swipe of the synthetic controller, which is the data slot contract (§13.2)
            std::vector<float> samples;
            for (int i = 0; i < 64; ++i)
                samples.push_back(static_cast<float>(std::sin(static_cast<double>(tick) / 4.0 + static_cast<double>(i) / 8.0)));
            // the immutable snapshot crosses into the widget, which is how the data type travels (§13.2)
            root.set_data(std::make_shared<const albor::WaveformData>(albor::WaveformData{.samples = std::move(samples)}));
            // the selection lands at the current time position, which the wired slot prints (§11.2)
            root.raise_selection(static_cast<float>(tick) / 2.0f);
            ++tick;
        },
        std::chrono::milliseconds(500));
    // the timer handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "chart: the ticker was not registered\n");
    // a closing window terminates the application, which is the ordinary shutdown path (§4.1)
    application.set_close_handler([&application](albor::WindowHandle) {
        application.quit(0);
        return true;
    });
    std::printf("chart: running, a new waveform and selection arrive every 500ms, close the window to quit\n");
    std::printf("chart: WaveformWidget generated from chart.ui-manifest, implemented by waveform_widget.cpp (§11.2, AD-17)\n");
    // the loop blocks until the window closes, which asks the application to quit (§4.1)
    return application.run();
}
