#include <chrono>
#include <cstdio>
#include <memory>

#include "application/application.h"
#include "drivability.h"
#include "greeting_panel.h"
#include "input/input_controller.h"
#include "render/frame_scene.h"
#include "render/render_executor.h"
#include "runtime/binding_engine.h"
#include "style/theme.h"
#include "text/text_engine.h"
#include "ui/frame_pipeline.h"
#include "widgets/theme_controller.h"
#include "widgets/widget.h"
#include "window/window.h"

// the light accents, which are the window surface the system shows in light mode with the reported accent (§8.4)
albor::Accents light_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}; }

// the dark accents, which are the window surface the system shows in dark mode with the reported accent (§8.4)
albor::Accents dark_accents(albor::Color accent) { return albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}; }

int main() {
    // the platform and the backends installed themselves at load, so the composition creates what they registered
    std::unique_ptr<albor::RendererBackend> renderer = albor::create_renderer_backend();
    if (!renderer || !renderer->ready()) {
        std::fprintf(stderr, "greeting: the skia metal backend did not come up\n");
        return 1;
    }
    // the text engine shapes the label runs, which the widgets borrow from the application (§9.1)
    std::unique_ptr<albor::TextEngine> text_engine = albor::create_text_engine();
    if (!text_engine || !text_engine->ready()) {
        std::fprintf(stderr, "greeting: the skia text engine did not come up\n");
        return 1;
    }
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Greeting";
    options.size = albor::Size{800.0f, 600.0f};
    const albor::WindowHandle handle = application.create_window(options);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "greeting: the window could not be created\n");
        return 1;
    }
    albor::Window* window = application.window(handle);
    // the surface wraps the metal layer of the window, which is the macos presentation path (§6.3, AD-16)
    std::unique_ptr<albor::RenderSurface> surface = renderer->create_surface(window->native_handle(), window->metrics());
    if (!surface) {
        std::fprintf(stderr, "greeting: the render surface could not be created\n");
        return 1;
    }
    // the reactive engine owns the dependency graph that settles the bindings (§7.3)
    albor::BindingEngine engine;
    // the generated facade is the whole component, which needs the text engine for its labels (§9.1)
    auto panel = std::make_unique<albor::GreetingPanel>(&engine, text_engine.get());
    // the generated signal slot is wired here, which is how a host reacts to the button (§8.3)
    panel->set_on_run_clicked([] { std::printf("greeting: the run_clicked signal fired\n"); });
    // the pipeline owns the tree and drives stage one on the ui thread (§6.1, AD-18)
    albor::FramePipeline pipeline(engine, std::move(panel));
    // the starting scheme is the one the system reports, which a native application follows (§8.4)
    const albor::SystemAppearance appearance = window->system_appearance();
    // the controller builds the macos preset over the accents the system reports, which is the native control set (§8.4)
    albor::ThemeController controller(albor::Accents{.surface = albor::Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f), .accent = appearance.accent, .outline = albor::Color::rgb(0.55f, 0.57f, 0.60f)}, albor::Accents{.surface = albor::Color::rgb(0.125f, 0.125f, 0.135f), .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f), .accent = appearance.accent, .outline = albor::Color::rgb(0.42f, 0.44f, 0.48f)}, albor::ThemePreset::Macos);
    // the controller owns the theme the whole tree borrows, so it is themed through the same presets (§6.1, §8.4)
    controller.install(pipeline.root(), appearance.scheme);
    // the scene is cleared with the themed surface, which is what the native faces read against (§6.1, §8.4)
    pipeline.set_background(controller.theme().surface());
    // the input controller routes the native events into the tree, which is the input path of §8.3
    albor::InputController input;
    input.attach(pipeline.root());
    // stage two runs on its own thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
    albor::RenderExecutor executor(renderer.get());
    pipeline.set_executor(&executor);
    pipeline.set_surface(surface.get());
    // the drivability is built over the live tree, which is what assistive technology and automation read (§10.1, §10.3)
    albor::example::Drivable drivable(pipeline.root(), window->native_handle().object());

    application.set_frame_handler([&] {
        // the window may already be gone when a queued tick arrives (§4.2)
        if (application.window(handle) == nullptr)
            return;
        // the metrics are read on every tick, so a resize is answered by the very next frame (§9.1)
        const albor::WindowMetrics metrics = window->metrics();
        // stage one settles the graph, lays out the tree and records the immutable scene (§6.1, §7.3)
        const std::shared_ptr<const albor::FrameScene> scene = pipeline.build(metrics);
        // the semantic tree is rebuilt after the bindings settled, then one client is served (§10.1, §10.3)
        drivable.refresh(); // stage two executes it off the ui thread, which never runs application code (§6.1, AD-18)
        pipeline.submit(scene);
    });
    // a native resize remeasures the tree, which is what the layout invalidation is for (§8.2)
    application.set_resize_handler([&](albor::WindowHandle resized) {
        // only the example window is answered, so another window is left alone (§5.2)
        if (resized != handle)
            return;
        // the executor drains first, because the surface may still be in a frame (§6.1, D9.1)
        executor.flush();
        // the surface is resized here on the ui thread, which is the thread the layer geometry belongs to (§5.3, D9.1)
        renderer->resize_surface(*surface, window->metrics());
        // the tree is remeasured, because the window now presents at other metrics (§8.2)
        pipeline.root().invalidate_layout();
    });
    // a native input event reaches the example, which forwards it into the tree of the one window (§8.3)
    application.set_input_handler([&](albor::WindowHandle source, const albor::Event& event) {
        // only the example window is routed, so another window is left alone (§5.2)
        if (source != handle)
            return;
        // the controller routes the event into the widget tree, which a click on the button answers (§8.3)
        albor::Event routed = event;
        input.deliver(routed);
    });
    // a timer advances the count, which the bound labels announce without any input (§4.2, §7.3)
    const albor::TimerHandle ticker = application.timers().schedule_repeating(
        [&] {
            // the window may already be gone when a queued tick arrives (§4.2)
            if (application.window(handle) == nullptr)
                return;
            // the generated facade publishes the accessor, which a host drives imperatively (§7.3, AD-24)
            albor::GreetingPanel& root = static_cast<albor::GreetingPanel&>(pipeline.root());
            root.set_click_count(root.click_count() + 1);
        },
        std::chrono::milliseconds(500));
    // the timer handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "greeting: the ticker was not registered\n");
    // a closing window terminates the application, which is the ordinary shutdown path (§4.1)
    application.set_close_handler([&application](albor::WindowHandle) {
        application.quit(0);
        return true;
    });
    std::printf("greeting: running, the count advances every 500ms, close the window to quit\n");
    // the panel loads the builtin tree, which is the code the driver generated from greeting_panel.ui (§11.3, AD-03a)
    std::printf("greeting: GreetingPanel generated from greeting_panel.ui, click Increment to raise the signal\n");

    // a system appearance change rebuilds and reinstalls the theme, so the window follows the settings live (§8.4)
    application.set_appearance_handler([&](albor::WindowHandle changed, const albor::SystemAppearance& changed_appearance) {
        // only the example window is answered, so another window is left alone (§5.2)
        if (changed != handle || application.window(handle) == nullptr)
            return;
        // the accents are rebuilt from the reported accent, and the scheme follows the report (§8.4)
        controller.set_accents(light_accents(changed_appearance.accent), dark_accents(changed_appearance.accent));
        controller.set_scheme(changed_appearance.scheme);
        // the frame background follows the surface the theme resolved (§8.4)
        pipeline.set_background(controller.theme().surface());
    });
    // the loop blocks until the window closes, which asks the application to quit (§4.1)
    return application.run();
}
