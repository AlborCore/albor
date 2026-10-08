#include <cstdio>
#include <memory>
#include <vector>

#include "application/application.h"
#include "platform/macos/event_loop.h"
#include "render/frame_scene.h"
#include "render/render-skia/skia_renderer_backend.h"
#include "render/render_executor.h"
#include "window/window.h"

namespace
{
    // the background the window is cleared with every frame (§6.1)
    const albor::Color BACKGROUND = albor::Color::rgb(0.06f, 0.07f, 0.09f);

    std::shared_ptr<const albor::FrameScene> build_frame(const albor::WindowMetrics& metrics, std::uint64_t index) {
        std::vector<albor::Rect> rects;
        // the inscribed band grows with the window, which is what makes a resize visible (§6.1)
        const float band_width = metrics.logical_size.width * 0.6f;
        const float band_height = metrics.logical_size.height * 0.25f;
        const float origin_x = (metrics.logical_size.width - band_width) * 0.5f;
        const float origin_y = (metrics.logical_size.height - band_height) * 0.5f;
        rects.push_back(albor::Rect::from_origin_size(albor::Point{origin_x, origin_y}, albor::Size{band_width, band_height}));
        // the sliding block proves that the frame advances rather than presenting a still image (§6.1)
        const float slide_width = band_width * 0.2f;
        const float travel = band_width - slide_width;
        const float phase = travel > 0.0f ? static_cast<float>(index % 120u) / 120.0f * travel : 0.0f;
        rects.push_back(albor::Rect::from_origin_size(albor::Point{origin_x + phase, origin_y + band_height * 0.25f}, albor::Size{slide_width, band_height * 0.5f}));
        // the scene is immutable once it exists, because the render thread consumes it (§6.1, §10.5)
        return std::make_shared<const albor::FrameScene>(albor::FrameScene::from_primitives(metrics, BACKGROUND, std::move(rects)));
    }
} // namespace

int main() {
    // the example is the composition root, so it installs the macos platform and the skia backend (§6.3)
    albor::macos::install_macos_platform();
    albor::install_skia_renderer_backend();
    // the renderer backend owns the metal device and the skia context (§6.3)
    std::unique_ptr<albor::RendererBackend> renderer = albor::create_renderer_backend();
    if (!renderer || !renderer->ready()) {
        std::fprintf(stderr, "hello-window: the skia metal backend did not come up\n");
        return 1;
    }
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the documented title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Hello Window";
    options.size = albor::Size{800.0f, 600.0f};
    const albor::WindowHandle handle = application.create_window(options);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "hello-window: the window could not be created\n");
        return 1;
    }
    albor::Window* window = application.window(handle);
    // the surface wraps the metal layer of the window, which is the macos presentation path (§6.3, AD-16)
    std::unique_ptr<albor::RenderSurface> surface = renderer->create_surface(window->native_handle(), window->metrics());
    if (!surface) {
        std::fprintf(stderr, "hello-window: the render surface could not be created\n");
        return 1;
    }
    // stage two runs on its own thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
    albor::RenderExecutor executor(renderer.get());
    // a closing window terminates the application, which is the ordinary shutdown path (§4.1)
    application.set_close_handler([&application](albor::WindowHandle) {
        application.quit(0);
        return true;
    });
    // the ui thread builds a frame per pacing tick and hands it to the render thread (§6.1)
    std::uint64_t frame_index = 0;
    application.set_frame_handler([&] {
        // the window may already be gone when a queued tick arrives (§4.2)
        if (application.window(handle) == nullptr)
            return;
        // the metrics are read on every tick, so a resize is answered by the very next frame (§9.1)
        const albor::WindowMetrics metrics = window->metrics();
        // stage one builds the immutable scene for the current metrics (§6.1)
        const std::shared_ptr<const albor::FrameScene> scene = build_frame(metrics, frame_index);
        // stage two executes it off the ui thread, which never runs application code (§6.1, AD-18)
        executor.submit(*surface, scene);
        frame_index++;
    });
    // a native resize resizes the surface here on the ui thread, which is the thread the layer geometry belongs to (§5.3, D9.1)
    application.set_resize_handler([&](albor::WindowHandle resized) {
        // only the example window is answered, so another window is left alone (§5.2)
        if (resized != handle)
            return;
        // the executor drains first, because the surface the resize writes to may still be in a frame that is executing (§6.1, D9.1)
        executor.flush();
        // the surface takes the metrics the window just reported, which is what the next scene is built for (§9.1)
        renderer->resize_surface(*surface, window->metrics());
    });
    std::printf("hello-window: running, close the window to quit\n");
    // the loop blocks until the window closes, which asks the application to quit (§4.1)
    return application.run();
}
