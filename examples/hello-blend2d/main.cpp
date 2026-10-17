#include <cstdio>
#include <memory>
#include <vector>

#include "application/application.h"
#include "render/frame_scene.h"

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
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the documented title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Hello Blend2D";
    options.size = albor::Size{800.0f, 600.0f};
    // this example owns its scene rather than a tree, which is what the scene source is for (§6.1)
    albor::MainWindowConfig config;
    config.background = BACKGROUND;
    config.scene_source = build_frame;
    // the blend2d backend installed itself at load, so linking chose it rather than code (§6.3, AD-26)
    const albor::WindowHandle handle = application.create_window(options, albor::RootBuilder{}, config);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "hello-blend2d: the window could not be created\n");
        return 1;
    }
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
